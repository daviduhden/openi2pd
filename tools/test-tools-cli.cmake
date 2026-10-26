# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test tools CLI.
#
# Focused command-line contracts: exact exit codes, stdout/stderr separation,
# refusal to overwrite key material and single-line registration records. It
# runs only in the manual CTest job; no network access and no long searches.
#

if(NOT DEFINED TOOLS_DIR)
  message(FATAL_ERROR "TOOLS_DIR is required")
endif()
if(NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is required")
endif()

set(EXE "${CMAKE_EXECUTABLE_SUFFIX}")
set(WORK "${WORK_DIR}/tools-cli")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
if(UNIX)
  file(CHMOD "${WORK}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endif()

function(fail message_text)
  file(REMOVE_RECURSE "${WORK}")
  message(FATAL_ERROR "${message_text}")
endfunction()

function(run_cli name out_var err_var)
  if(DEFINED TOOL_STDIN AND NOT "${TOOL_STDIN}" STREQUAL "")
    execute_process(
      COMMAND "${TOOLS_DIR}/${name}${EXE}" ${ARGN}
      WORKING_DIRECTORY "${WORK}"
      INPUT_FILE "${TOOL_STDIN}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE error
      TIMEOUT 30)
  else()
    execute_process(
      COMMAND "${TOOLS_DIR}/${name}${EXE}" ${ARGN}
      WORKING_DIRECTORY "${WORK}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE error
      TIMEOUT 30)
  endif()
  if(NOT "${result}" MATCHES "^-?[0-9]+$")
    fail("${name} did not exit normally (${result})")
  endif()
  set("${out_var}" "${output}" PARENT_SCOPE)
  set("${err_var}" "${error}" PARENT_SCOPE)
  set(LAST_RESULT "${result}" PARENT_SCOPE)
endfunction()

function(expect_code expected description)
  if(NOT "${LAST_RESULT}" EQUAL ${expected})
    fail("${description}: expected exit ${expected}, got ${LAST_RESULT}")
  endif()
endfunction()

function(expect_contains text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    fail("${description}: missing '${needle}' in:\n${text}")
  endif()
endfunction()

function(expect_empty text description)
  if(NOT "${text}" STREQUAL "")
    fail("${description}: expected empty, got:\n${text}")
  endif()
endfunction()

function(secret_hash path out_var)
  file(SHA256 "${path}" hash)
  set("${out_var}" "${hash}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# keygen: explicit help succeeds; a bad invocation is a stderr usage error;
# a leading-dash filename is never created.
# ---------------------------------------------------------------------------
run_cli(keygen out err --help)
expect_code(0 "keygen --help")
expect_contains("${out}" "Usage: keygen" "keygen --help stdout")
expect_empty("${err}" "keygen --help stderr")
if(EXISTS "${WORK}/--help")
  fail("keygen --help created a file")
endif()

run_cli(keygen out err)
expect_code(1 "keygen without arguments")
expect_empty("${out}" "keygen usage stdout")
expect_contains("${err}" "Usage: keygen" "keygen usage stderr")

run_cli(keygen out err -x)
expect_code(1 "keygen leading-dash filename")
expect_contains("${err}" "Usage: keygen" "keygen leading-dash usage")
if(EXISTS "${WORK}/-x")
  fail("keygen created a file named -x")
endif()

# ---------------------------------------------------------------------------
# keyinfo: a read failure is stderr/exit 2; a non-blindable destination under
# -b is an error, not a successful partial listing.
# ---------------------------------------------------------------------------
run_cli(keyinfo out err missing.keys)
expect_code(2 "keyinfo missing file")
expect_empty("${out}" "keyinfo missing stdout")
expect_contains("${err}" "cannot open" "keyinfo missing stderr")

run_cli(keygen out err p256.keys ECDSA-P256)
expect_code(0 "keygen P-256")
run_cli(keyinfo out err -b p256.keys)
expect_code(3 "keyinfo -b non-blindable")
expect_contains("${err}" "Invalid signature type" "keyinfo -b stderr")
expect_empty("${out}" "keyinfo -b non-blindable stdout")

# ---------------------------------------------------------------------------
# routerinfo: help on stdout, usage errors on stderr, no partial stdout.
# ---------------------------------------------------------------------------
run_cli(routerinfo out err --help)
expect_code(0 "routerinfo --help")
expect_contains("${out}" "usage: routerinfo" "routerinfo help stdout")
expect_empty("${err}" "routerinfo help stderr")

run_cli(routerinfo out err)
expect_code(1 "routerinfo without operands")
expect_empty("${out}" "routerinfo usage stdout")
expect_contains("${err}" "usage: routerinfo" "routerinfo usage stderr")

run_cli(routerinfo out err --firewall=bogus)
expect_code(1 "routerinfo unknown backend")
expect_empty("${out}" "routerinfo backend stdout")
expect_contains("${err}" "unknown firewall backend" "routerinfo backend stderr")

# ---------------------------------------------------------------------------
# Same-file refusal: the destination key file must survive unchanged.
# ---------------------------------------------------------------------------
run_cli(keygen out err source.keys EDDSA-SHA512-ED25519)
expect_code(0 "keygen source")
secret_hash("${WORK}/source.keys" before)

run_cli(offlinekeys out err source.keys source.keys)
expect_code(1 "offlinekeys same input and output")
expect_contains("${err}" "refusing" "offlinekeys same-file stderr")
secret_hash("${WORK}/source.keys" after)
if(NOT "${before}" STREQUAL "${after}")
  fail("offlinekeys modified its own input key file")
endif()

run_cli(b33offlinekeys out err source.keys source.keys)
expect_code(1 "b33offlinekeys same input and output")
expect_contains("${err}" "refusing" "b33offlinekeys same-file stderr")
secret_hash("${WORK}/source.keys" after)
if(NOT "${before}" STREQUAL "${after}")
  fail("b33offlinekeys modified its own input key file")
endif()

# ---------------------------------------------------------------------------
# b33address: an embedded NUL is rejected and never reaches stdout.
# ---------------------------------------------------------------------------
file(WRITE "${WORK}/nul.b64" "AAAA\0BBBB\n")
set(TOOL_STDIN "${WORK}/nul.b64")
run_cli(b33address out err)
unset(TOOL_STDIN)
expect_code(1 "b33address NUL input")
if(NOT "${out}" STREQUAL "")
  fail("b33address emitted data for a NUL input:\n${out}")
endif()

# ---------------------------------------------------------------------------
# i2pbase64: padding terminates the stream, even across a chunk boundary.
# ---------------------------------------------------------------------------
file(WRITE "${WORK}/padding.txt" "AA==\nAAAA\n")
set(TOOL_STDIN "${WORK}/padding.txt")
run_cli(i2pbase64 out err -d)
unset(TOOL_STDIN)
expect_code(1 "i2pbase64 data after padding")
expect_contains("${err}" "after padding" "i2pbase64 padding stderr")

# ---------------------------------------------------------------------------
# famtool: a mode is required.
# ---------------------------------------------------------------------------
run_cli(famtool out err -n family)
expect_code(1 "famtool missing mode")
expect_contains("${err}" "no mode selected" "famtool missing mode stderr")

# ---------------------------------------------------------------------------
# regaddr_3ld: a CRLF step file signs exactly one LF-only record and the
# paired verifier accepts it.
# ---------------------------------------------------------------------------
run_cli(keygen out err reg.keys EDDSA-SHA512-ED25519)
expect_code(0 "keygen registration key")
run_cli(regaddr_3ld out err step1 reg.keys example.i2p)
expect_code(0 "regaddr_3ld step1")
string(STRIP "${out}" step1)
file(WRITE "${WORK}/step1.txt" "${step1}\r\n")

run_cli(regaddr_3ld out err step2 step1.txt reg.keys old.i2p)
expect_code(0 "regaddr_3ld step2")
string(STRIP "${out}" step2)
string(FIND "${step2}" "\n" newline)
if(NOT newline EQUAL -1)
  fail("regaddr_3ld step2 emitted more than one record")
endif()
file(WRITE "${WORK}/step2.txt" "${step2}\n")

run_cli(regaddr_3ld out err step3 step2.txt reg.keys)
expect_code(0 "regaddr_3ld step3")
string(STRIP "${out}" step3)
string(FIND "${step3}" "\n" newline)
if(NOT newline EQUAL -1)
  fail("regaddr_3ld step3 emitted more than one record")
endif()
run_cli(verifyhost out err "${step3}")
expect_code(0 "verifyhost accepts the CRLF-normalized record")
expect_contains("${out}${err}" "Valid destination" "verifyhost verdict")

# ---------------------------------------------------------------------------
# Registration names cannot forge extra record fields.
# ---------------------------------------------------------------------------
run_cli(regaddr out err reg.keys "bad#name.i2p")
expect_code(2 "regaddr rejects '#' in the name")
expect_contains("${err}" "address must not" "regaddr name stderr")

file(REMOVE_RECURSE "${WORK}")
message(STATUS "tools-cli: all checks passed")
