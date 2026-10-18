# Smoke test for the integrated i2pd-tools. No network access.
if(NOT DEFINED TOOLS_DIR)
  message(FATAL_ERROR "TOOLS_DIR is required")
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
if(NOT LAST_TOOL_OUTPUT MATCHES "--firewall=nftables" OR NOT LAST_TOOL_OUTPUT MATCHES "--firewall=windows" OR NOT LAST_TOOL_OUTPUT MATCHES "--firewall=pf")
  message(FATAL_ERROR "routerinfo help does not document all backends")
endif()

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

execute_process(COMMAND "${TOOLS_DIR}/routerinfo${EXE}" --firewall=pf "--nft-chain=output" keys.dat
  RESULT_VARIABLE nft_chain_without_nft)
if(nft_chain_without_nft EQUAL 0)
  message(FATAL_ERROR "routerinfo accepted nftables options with a non-nft backend")
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
