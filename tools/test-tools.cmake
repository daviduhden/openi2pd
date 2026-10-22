# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test tools.
#
# Offline CTest smoke test for the native tools: key generation, base64
# round-trip, host-record signing/verification and routerinfo firewall CLI
# validation. It runs in a temporary work directory and needs no network.
#

if(NOT DEFINED TOOLS_DIR)
  message(FATAL_ERROR "TOOLS_DIR is required")
endif()
if(NOT DEFINED ROUTERINFO_FIXTURE_HELPER OR NOT EXISTS "${ROUTERINFO_FIXTURE_HELPER}")
  message(FATAL_ERROR "ROUTERINFO_FIXTURE_HELPER must name the built fixture helper")
endif()
set(EXE "${CMAKE_EXECUTABLE_SUFFIX}")
set(WORK "${WORK_DIR}/tools-smoke")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run_tool name)
  execute_process(
    COMMAND "${TOOLS_DIR}/${name}${EXE}" ${ARGN}
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${name} failed (${result}): ${output}${error}")
  endif()
  set(LAST_TOOL_OUTPUT "${output}" PARENT_SCOPE)
endfunction()

run_tool(keygen keys.dat EDDSA-SHA512-ED25519)
if(NOT EXISTS "${WORK}/keys.dat")
  message(FATAL_ERROR "keygen did not create keys.dat")
endif()

run_tool(keyinfo -v keys.dat)
if(NOT LAST_TOOL_OUTPUT MATCHES "Signature Type: ED25519-SHA512")
  message(FATAL_ERROR "keyinfo did not report the generated key type")
endif()

run_tool(keyinfo -d keys.dat)
set(DESTINATION "${LAST_TOOL_OUTPUT}")
string(STRIP "${DESTINATION}" DESTINATION)

file(WRITE "${WORK}/plain.txt" "hello i2pd tools\n")
run_tool(i2pbase64 plain.txt)
file(WRITE "${WORK}/plain.b64" "${LAST_TOOL_OUTPUT}")
execute_process(
  COMMAND "${TOOLS_DIR}/i2pbase64${EXE}" -d plain.b64
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE decode_result
  OUTPUT_VARIABLE decoded)
if(NOT decode_result EQUAL 0 OR NOT decoded STREQUAL "hello i2pd tools\n")
  message(FATAL_ERROR "i2pbase64 round trip failed")
endif()

run_tool(regaddr keys.dat example.i2p)
set(RECORD "${LAST_TOOL_OUTPUT}")
string(STRIP "${RECORD}" RECORD)
execute_process(
  COMMAND "${TOOLS_DIR}/verifyhost${EXE}" "${RECORD}"
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE verify_result
  OUTPUT_VARIABLE verify_output)
if(NOT verify_result EQUAL 0)
  message(FATAL_ERROR "verifyhost rejected regaddr output: ${verify_output}")
endif()

run_tool(offlinekeys offline.dat keys.dat EDDSA-SHA512-ED25519 30)
if(NOT EXISTS "${WORK}/offline.dat")
  message(FATAL_ERROR "offlinekeys did not create its output")
endif()

run_tool(x25519)
if(NOT LAST_TOOL_OUTPUT MATCHES "PublicKey:" OR NOT LAST_TOOL_OUTPUT MATCHES "PrivateKey:")
  message(FATAL_ERROR "x25519 output is missing keys")
endif()

run_tool(routerinfo --help)
if(NOT LAST_TOOL_OUTPUT MATCHES "--firewall=iptables"
   OR NOT LAST_TOOL_OUTPUT MATCHES "--firewall=nftables"
   OR NOT LAST_TOOL_OUTPUT MATCHES "--firewall=windows"
   OR NOT LAST_TOOL_OUTPUT MATCHES "--firewall=pf"
   OR NOT LAST_TOOL_OUTPUT MATCHES "-f")
  message(FATAL_ERROR "routerinfo help does not document all backends and legacy -f")
endif()

# Generate a valid, signed RouterInfo with documentation-range v4/v6
# endpoints through the production LocalRouterInfo serializer. The helper
# creates ephemeral keys in memory and writes only the public RouterInfo.
execute_process(COMMAND "${ROUTERINFO_FIXTURE_HELPER}" "${WORK}/valid-routerinfo.dat"
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE fixture_result
  OUTPUT_VARIABLE fixture_output
  ERROR_VARIABLE fixture_error)
if(NOT fixture_result EQUAL 0 OR NOT EXISTS "${WORK}/valid-routerinfo.dat")
  message(FATAL_ERROR "RouterInfo fixture helper failed (${fixture_result}): ${fixture_output}${fixture_error}")
endif()

# Positive CLI coverage for IPv6 address selection and both legacy/explicit
# iptables output families. TOOL-012 intentionally keeps IPv4 -f bytes stable
# and prefixes IPv6 commands with ip6tables.
run_tool(routerinfo -6 -p valid-routerinfo.dat)
foreach(expected
    "NTCP2: 192.0.2.10:12345"
    "SSU2: 198.51.100.20:12346"
    "NTCP2: 2001:db8::10:12347"
    "SSU2: 2001:db8::20:12348")
  string(FIND "${LAST_TOOL_OUTPUT}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "routerinfo -6 -p omitted '${expected}': ${LAST_TOOL_OUTPUT}")
  endif()
endforeach()

run_tool(routerinfo -6 -f valid-routerinfo.dat)
foreach(expected
    " -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT"
    " -A OUTPUT -p udp -d 198.51.100.20 --dport 12346 -j ACCEPT"
    "ip6tables -A OUTPUT -p tcp -d 2001:db8::10 --dport 12347 -j ACCEPT"
    "ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 12348 -j ACCEPT")
  string(FIND "${LAST_TOOL_OUTPUT}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "routerinfo -6 -f omitted '${expected}': ${LAST_TOOL_OUTPUT}")
  endif()
endforeach()

run_tool(routerinfo --firewall=iptables valid-routerinfo.dat)
foreach(expected
    "iptables -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT"
    "iptables -A OUTPUT -p udp -d 198.51.100.20 --dport 12346 -j ACCEPT")
  string(FIND "${LAST_TOOL_OUTPUT}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "routerinfo --firewall=iptables omitted '${expected}': ${LAST_TOOL_OUTPUT}")
  endif()
endforeach()
string(FIND "${LAST_TOOL_OUTPUT}" "2001:db8::" ipv6_position)
if(NOT ipv6_position EQUAL -1)
  message(FATAL_ERROR "routerinfo selected IPv6 without -6: ${LAST_TOOL_OUTPUT}")
endif()

run_tool(routerinfo -6 --firewall=iptables valid-routerinfo.dat)
foreach(expected
    "iptables -A OUTPUT -p tcp -d 192.0.2.10 --dport 12345 -j ACCEPT"
    "iptables -A OUTPUT -p udp -d 198.51.100.20 --dport 12346 -j ACCEPT"
    "ip6tables -A OUTPUT -p tcp -d 2001:db8::10 --dport 12347 -j ACCEPT"
    "ip6tables -A OUTPUT -p udp -d 2001:db8::20 --dport 12348 -j ACCEPT")
  string(FIND "${LAST_TOOL_OUTPUT}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "routerinfo --firewall=iptables omitted '${expected}': ${LAST_TOOL_OUTPUT}")
  endif()
endforeach()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=bogus
  RESULT_VARIABLE bad_backend)
if(bad_backend EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted an unknown firewall backend")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=nftables "--nft-table=bad\;name" keys.dat
  RESULT_VARIABLE bad_table)
if(bad_table EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted an unsafe nftables table name")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" -f --firewall=pf keys.dat
  RESULT_VARIABLE conflicting)
if(conflicting EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted conflicting firewall options")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" -f --firewall=pf
  RESULT_VARIABLE conflicting_no_operand)
if(conflicting_no_operand EQUAL 0)
  message(FATAL_ERROR "routerinfo reported usage before the -f/--firewall conflict")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" -f --nft-family=ip keys.dat
  RESULT_VARIABLE legacy_nft)
if(legacy_nft EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted -f with nftables options")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=nftables --nft-family=ip6 keys.dat
  RESULT_VARIABLE nft_ip6_without_v6)
if(nft_ip6_without_v6 EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted --nft-family=ip6 without -6")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" -y --firewall=pf keys.dat
  RESULT_VARIABLE yggdrasil_without_v6)
if(yggdrasil_without_v6 EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted -y firewall output without -6")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" "--nft-family=ip" keys.dat
  RESULT_VARIABLE nft_without_backend)
if(nft_without_backend EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted nftables options without --firewall=nftables")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=nftables --nft-family=inet6 keys.dat
  RESULT_VARIABLE bad_family)
if(bad_family EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted an invalid nftables family")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=nftables --nft-chain=input keys.dat
  RESULT_VARIABLE inbound_chain)
if(inbound_chain EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted an inbound nftables chain")
endif()

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=pf "--nft-chain=output" keys.dat
  RESULT_VARIABLE nft_chain_without_nft)
if(nft_chain_without_nft EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted nftables options with a non-nft backend")
endif()

# A malformed RouterInfo must be rejected before hash/address access in every
# backend, including the legacy -f path.
file(WRITE "${WORK}/malformed-routerinfo.dat" "not a RouterInfo\n")
foreach(firewall_mode iptables nftables windows pf)
  execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}"
    "--firewall=${firewall_mode}" "${WORK}/malformed-routerinfo.dat"
    RESULT_VARIABLE malformed_routerinfo)
  if(malformed_routerinfo EQUAL 0)
    message(FATAL_ERROR "routerinfo accepted a malformed RouterInfo with ${firewall_mode}")
  endif()
endforeach()
execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" -f
  "${WORK}/malformed-routerinfo.dat"
  RESULT_VARIABLE malformed_legacy)
if(malformed_legacy EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted a malformed RouterInfo with legacy -f")
endif()

# vain: help and validation paths only; never start a search in the smoke test.
run_tool(vain --help)
if(NOT LAST_TOOL_OUTPUT MATCHES "--multiplymode" OR NOT LAST_TOOL_OUTPUT MATCHES "--threads")
  message(FATAL_ERROR "vain help is incomplete")
endif()

execute_process(COMMAND "${TOOLS_DIR}/vain${EXE}" -t 0 example
  RESULT_VARIABLE bad_threads)
if(bad_threads EQUAL 0)
  message(FATAL_ERROR "vain accepted a zero thread count")
endif()

execute_process(COMMAND "${TOOLS_DIR}/vain${EXE}" -r "("
  RESULT_VARIABLE bad_regex)
if(bad_regex EQUAL 0)
  message(FATAL_ERROR "vain accepted an invalid regular expression")
endif()

execute_process(COMMAND "${TOOLS_DIR}/vain${EXE}" "NOT-BASE32"
  RESULT_VARIABLE bad_prefix)
if(bad_prefix EQUAL 0)
  message(FATAL_ERROR "vain accepted a non-base32 prefix")
endif()

execute_process(COMMAND "${TOOLS_DIR}/vain${EXE}" -s ECDSA-P256 example
  RESULT_VARIABLE bad_signature)
if(bad_signature EQUAL 0)
  message(FATAL_ERROR "vain accepted an unsupported signature type")
endif()

file(REMOVE_RECURSE "${WORK}")
