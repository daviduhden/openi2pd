# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test tools regressions.
#
# Deterministic per-tool CTest regressions for key types, offline/b33 keys,
# registration records and firewall CLI errors. Each run is time-limited;
# network access, external interpreters and long-running searches are avoided,
# and ephemeral keys are removed.
#

if(NOT DEFINED TOOLS_DIR)
  message(FATAL_ERROR "TOOLS_DIR is required")
endif()
if(NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "WORK_DIR is required")
endif()

set(EXE "${CMAKE_EXECUTABLE_SUFFIX}")
set(WORK "${WORK_DIR}/tools-regressions")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
if(UNIX)
  file(CHMOD "${WORK}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endif()

function(fail message_text)
  file(REMOVE_RECURSE "${WORK}")
  message(FATAL_ERROR "${message_text}")
endfunction()

function(restrict_secret path)
  if(UNIX AND EXISTS "${path}")
    file(CHMOD "${path}" PERMISSIONS OWNER_READ OWNER_WRITE)
  endif()
endfunction()

# Run one tool. TOOL_STDIN, when set to a non-empty path in the caller's
# scope, is connected to the process stdin. A per-process timeout keeps a
# regression from hanging a CI job even if a tool starts waiting for input.
function(run_tool name expect_success)
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
  if("${result}" STREQUAL "Process terminated due to timeout")
    fail("${name} exceeded the 30 s timeout")
  endif()
  if(NOT "${result}" MATCHES "^-?[0-9]+$")
    fail("${name} did not exit normally (${result}):\n${output}${error}")
  endif()
  if(expect_success)
    if(NOT "${result}" EQUAL 0)
      fail("${name} failed (${result}):\n${output}${error}")
    endif()
  elseif("${result}" EQUAL 0)
    fail("${name} accepted invalid input (exit 0):\n${output}${error}")
  endif()
  set(LAST_TOOL_OUTPUT "${output}" PARENT_SCOPE)
  set(LAST_TOOL_ERROR "${error}" PARENT_SCOPE)
  set(LAST_TOOL_RESULT "${result}" PARENT_SCOPE)
endfunction()

# Run one tool whose stdout is written to a file. Used for byte-exact binary
# comparisons so CMake never has to carry arbitrary bytes in a variable.
function(run_tool_to_file name output_file expect_success)
  execute_process(
    COMMAND "${TOOLS_DIR}/${name}${EXE}" ${ARGN}
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${output_file}"
    ERROR_VARIABLE error
    TIMEOUT 30)
  if("${result}" STREQUAL "Process terminated due to timeout")
    fail("${name} exceeded the 30 s timeout")
  endif()
  if(NOT "${result}" MATCHES "^-?[0-9]+$")
    fail("${name} did not exit normally (${result}):\n${error}")
  endif()
  if(expect_success)
    if(NOT "${result}" EQUAL 0)
      fail("${name} failed (${result}):\n${error}")
    endif()
  elseif("${result}" EQUAL 0)
    fail("${name} accepted invalid input (exit 0):\n${error}")
  endif()
  set(LAST_TOOL_OUTPUT "" PARENT_SCOPE)
  set(LAST_TOOL_ERROR "${error}" PARENT_SCOPE)
  set(LAST_TOOL_RESULT "${result}" PARENT_SCOPE)
endfunction()

# Messages may be written to either stream; the tools are inconsistent about
# it, and both are part of the observable CLI contract.
function(assert_output_matches pattern description)
  if(NOT "${LAST_TOOL_OUTPUT}${LAST_TOOL_ERROR}" MATCHES "${pattern}")
    fail("${description}: output does not match /${pattern}/\nstdout:\n${LAST_TOOL_OUTPUT}\nstderr:\n${LAST_TOOL_ERROR}")
  endif()
endfunction()

function(assert_output_contains needle description)
  string(FIND "${LAST_TOOL_OUTPUT}${LAST_TOOL_ERROR}" "${needle}" position)
  if(position EQUAL -1)
    fail("${description}: output does not contain '${needle}'\nstdout:\n${LAST_TOOL_OUTPUT}\nstderr:\n${LAST_TOOL_ERROR}")
  endif()
endfunction()

function(assert_contains text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    fail("${description}: value does not contain '${needle}'\nvalue:\n${text}")
  endif()
endfunction()

function(assert_value_matches value pattern description)
  if(NOT "${value}" MATCHES "${pattern}")
    fail("${description}: value does not match /${pattern}/\nvalue:\n${value}")
  endif()
endfunction()

function(assert_stripped_output_equals expected description)
  string(STRIP "${LAST_TOOL_OUTPUT}" stripped)
  if(NOT "${stripped}" STREQUAL "${expected}")
    fail("${description}: expected '${expected}', got '${stripped}'")
  endif()
endfunction()

function(extract_last_output pattern out_var description)
  if("${LAST_TOOL_OUTPUT}" MATCHES "${pattern}")
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
  else()
    fail("${description}: cannot extract /${pattern}/ from:\n${LAST_TOOL_OUTPUT}")
  endif()
endfunction()

function(assert_file_nonempty path description)
  if(NOT EXISTS "${path}")
    fail("${description}: missing ${path}")
  endif()
  file(SIZE "${path}" size)
  if(size LESS 1)
    fail("${description}: ${path} is empty")
  endif()
endfunction()

function(assert_file_contains_literal path needle description)
  if(NOT EXISTS "${path}")
    fail("${description}: missing ${path}")
  endif()
  file(READ "${path}" content)
  string(FIND "${content}" "${needle}" position)
  if(position EQUAL -1)
    fail("${description}: ${path} does not contain '${needle}'")
  endif()
endfunction()

function(assert_file_hex path expected description)
  file(READ "${path}" actual HEX)
  if(NOT "${actual}" STREQUAL "${expected}")
    fail("${description}: expected hex '${expected}', got '${actual}'")
  endif()
endfunction()

# ---------------------------------------------------------------------------
# keyinfo: several signing-key types and every output option.
# ---------------------------------------------------------------------------
run_tool(keygen TRUE ed25519.keys EDDSA-SHA512-ED25519)
assert_output_contains("Signature type: ED25519-SHA512" "keygen ED25519")
restrict_secret("${WORK}/ed25519.keys")

run_tool(keygen TRUE reddsa.keys RED25519-SHA512)
assert_output_contains("Signature type: RED25519-SHA512" "keygen RedDSA")
restrict_secret("${WORK}/reddsa.keys")

run_tool(keygen TRUE p256.keys ECDSA-P256)
assert_output_contains("Signature type: ECDSA-P256" "keygen P-256")
restrict_secret("${WORK}/p256.keys")

run_tool(keyinfo TRUE -v ed25519.keys)
assert_output_matches("Signature Type: ED25519-SHA512" "keyinfo -v Ed25519 signature type")
assert_output_matches("Encryption Type: 0" "keyinfo -v encryption type")
assert_output_matches("Destination: [-A-Za-z0-9~]+=*" "keyinfo -v destination")
assert_output_matches("Destination Hash: [-A-Za-z0-9~]+=*" "keyinfo -v destination hash")
assert_output_matches("B32 Address: [a-z2-7]+\\.b32\\.i2p" "keyinfo -v b32 address")

run_tool(keyinfo TRUE ed25519.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" ED_B32)
assert_value_matches("${ED_B32}" "^[a-z2-7]+\\.b32\\.i2p$" "keyinfo default output")
string(REPLACE ".b32.i2p" "" ED_B32_HASH "${ED_B32}")
string(LENGTH "${ED_B32_HASH}" ED_B32_LENGTH)
if(NOT ED_B32_LENGTH EQUAL 52)
  fail("keyinfo default output is not a 52-character b32 address: ${ED_B32}")
endif()

run_tool(keyinfo TRUE -d ed25519.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" ED_DEST)
assert_value_matches("${ED_DEST}" "^[-A-Za-z0-9~]+=*$" "keyinfo -d destination encoding")

run_tool(keyinfo TRUE -p ed25519.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" ED_PRIVATE)
assert_value_matches("${ED_PRIVATE}" "^[-A-Za-z0-9~]+=*$" "keyinfo -p private encoding")
if("${ED_PRIVATE}" STREQUAL "${ED_DEST}")
  fail("keyinfo -p returned the public destination")
endif()

run_tool(keyinfo TRUE -b ed25519.keys)
assert_output_matches("b33 address: [a-z2-7]+\\.b32\\.i2p" "keyinfo -b Ed25519 b33 address")
assert_output_matches("Today's store hash: [-A-Za-z0-9~]+=*" "keyinfo -b store hash")
extract_last_output("b33 address: ([a-z2-7]+)\\.b32\\.i2p" ED_B33 "keyinfo -b Ed25519")
assert_value_matches("${ED_B33}" "^[a-z2-7]+$" "keyinfo -b b33 encoding")

run_tool(keyinfo TRUE -v -b ed25519.keys)
assert_output_contains("Signature Type: ED25519-SHA512" "keyinfo combined -v -b signature")
assert_output_contains("b33 address: " "keyinfo combined -v -b b33")

run_tool(keyinfo TRUE -v reddsa.keys)
assert_output_matches("Signature Type: RED25519-SHA512" "keyinfo -v RedDSA signature type")
run_tool(keyinfo TRUE -b reddsa.keys)
assert_output_matches("b33 address: [a-z2-7]+\\.b32\\.i2p" "keyinfo -b RedDSA b33 address")
extract_last_output("b33 address: ([a-z2-7]+)\\.b32\\.i2p" RED_B33 "keyinfo -b RedDSA")
if("${RED_B33}" STREQUAL "${ED_B33}")
  fail("keyinfo -b returned the same b33 address for different keys")
endif()

run_tool(keyinfo TRUE -v p256.keys)
assert_output_matches("Signature Type: ECDSA-P256" "keyinfo -v P-256 signature type")
run_tool(keyinfo TRUE -b p256.keys)
assert_output_matches("Invalid signature type ECDSA-P256" "keyinfo -b rejects non-blindable P-256")

run_tool(keyinfo TRUE -h)
assert_output_contains("usage:" "keyinfo -h")
run_tool(keyinfo FALSE)
assert_output_contains("usage:" "keyinfo without arguments")
run_tool(keyinfo FALSE -z ed25519.keys)
run_tool(keyinfo FALSE ed25519.keys extra)
assert_output_contains("unexpected extra argument" "keyinfo extra argument")
run_tool(keyinfo FALSE missing.keys)

file(WRITE "${WORK}/malformed.keys" "not a PrivateKeys file\n")
run_tool(keyinfo FALSE malformed.keys)
assert_output_contains("bad key file format" "keyinfo malformed file")
file(WRITE "${WORK}/empty.keys" "")
run_tool(keyinfo FALSE empty.keys)
assert_output_contains("bad key file format" "keyinfo empty file")

# ---------------------------------------------------------------------------
# b33address: Ed25519/RedDSA input, stdin, and invalid-input paths.
# ---------------------------------------------------------------------------
file(WRITE "${WORK}/ed25519.dest" "${ED_DEST}\n")
set(TOOL_STDIN "${WORK}/ed25519.dest")
run_tool(b33address TRUE)
unset(TOOL_STDIN)
assert_output_matches("b33 address: [a-z2-7]+\\.b32\\.i2p" "b33address Ed25519 address")
assert_output_matches("Today's store hash: [-A-Za-z0-9~]+=*" "b33address Ed25519 store hash")
extract_last_output("b33 address: ([a-z2-7]+)\\.b32\\.i2p" ADDRESS_B33 "b33address Ed25519")
assert_value_matches("${ADDRESS_B33}" "^[a-z2-7]+$" "b33address address encoding")

run_tool(keyinfo TRUE -d reddsa.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" RED_DEST)
file(WRITE "${WORK}/reddsa.dest" "${RED_DEST}\n")
set(TOOL_STDIN "${WORK}/reddsa.dest")
run_tool(b33address TRUE)
unset(TOOL_STDIN)
assert_output_matches("b33 address: [a-z2-7]+\\.b32\\.i2p" "b33address RedDSA address")

file(WRITE "${WORK}/invalid.dest" "this is not base64 !!!\n")
set(TOOL_STDIN "${WORK}/invalid.dest")
run_tool(b33address TRUE)
unset(TOOL_STDIN)
assert_output_contains("Invalid base64 address" "b33address invalid base64")

file(WRITE "${WORK}/empty.dest" "")
set(TOOL_STDIN "${WORK}/empty.dest")
run_tool(b33address TRUE)
unset(TOOL_STDIN)
assert_output_contains("Invalid base64 address" "b33address empty input")

run_tool(keyinfo TRUE -d p256.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" P256_DEST)
file(WRITE "${WORK}/p256.dest" "${P256_DEST}\n")
set(TOOL_STDIN "${WORK}/p256.dest")
run_tool(b33address TRUE)
unset(TOOL_STDIN)
assert_output_contains("Invalid signature type ECDSA-P256" "b33address rejects non-blindable P-256")

string(REPEAT "A" 5000 LONG_B64)
file(WRITE "${WORK}/long.dest" "${LONG_B64}\n")
set(TOOL_STDIN "${WORK}/long.dest")
run_tool(b33address TRUE)
unset(TOOL_STDIN)
assert_output_contains("input too long" "b33address rejects oversized stdin")

# ---------------------------------------------------------------------------
# b33offlinekeys: bounded day counts, output round trip and error paths.
# ---------------------------------------------------------------------------
# 30 days rather than 1: the outer offline signature expires at the next
# midnight for a one-day batch, so parsing it later in the same test run would
# be a (very unlikely) midnight race.
run_tool(b33offlinekeys TRUE b33-batch.dat ed25519.keys 30)
assert_output_matches("Address [a-z2-7]+\\.b32\\.i2p, 30 days" "b33offlinekeys output")
assert_file_nonempty("${WORK}/b33-batch.dat" "b33offlinekeys output file")
restrict_secret("${WORK}/b33-batch.dat")

run_tool(keyinfo TRUE -v b33-batch.dat)
assert_output_contains("Offline signature" "keyinfo reads b33offlinekeys offline signature")
assert_output_contains("Transient Signature Type: ED25519-SHA512" "keyinfo b33 transient type")
assert_output_contains("Expires: " "keyinfo b33 expiry")
run_tool(keyinfo TRUE -b b33-batch.dat)
assert_output_contains("b33 offline keys: 30 days" "keyinfo parses the b33 offline batch")

run_tool(b33offlinekeys TRUE b33-default.dat ed25519.keys)
assert_output_matches("Address [a-z2-7]+\\.b32\\.i2p, 365 days" "b33offlinekeys default days")
assert_file_nonempty("${WORK}/b33-default.dat" "b33offlinekeys default output file")
restrict_secret("${WORK}/b33-default.dat")

run_tool(b33offlinekeys FALSE)
assert_output_contains("Usage: b33offlinekeys" "b33offlinekeys without arguments")
run_tool(b33offlinekeys FALSE bad.dat ed25519.keys 0)
assert_output_contains("days must be 1..65535" "b33offlinekeys zero days")
run_tool(b33offlinekeys FALSE bad.dat ed25519.keys 65536)
assert_output_contains("days must be 1..65535" "b33offlinekeys upper bound")
run_tool(b33offlinekeys FALSE bad.dat ed25519.keys -1)
assert_output_contains("days must be 1..65535" "b33offlinekeys negative days")
run_tool(b33offlinekeys FALSE bad.dat ed25519.keys abc)
assert_output_contains("days must be 1..65535" "b33offlinekeys non-numeric days")
run_tool(b33offlinekeys FALSE bad.dat ed25519.keys 1x)
assert_output_contains("days must be 1..65535" "b33offlinekeys trailing characters")

run_tool(b33offlinekeys FALSE bad.dat p256.keys 1)
assert_output_contains("has no b33 address" "b33offlinekeys rejects P-256")
run_tool(b33offlinekeys FALSE bad.dat b33-batch.dat 1)
assert_output_contains("holds offline keys" "b33offlinekeys rejects an offline key file")
run_tool(b33offlinekeys FALSE bad.dat missing.keys 1)
run_tool(b33offlinekeys FALSE bad.dat malformed.keys 1)
assert_output_contains("bad keys file format" "b33offlinekeys malformed key file")
run_tool(b33offlinekeys FALSE bad.dat empty.keys 1)
assert_output_contains("bad keys file format" "b33offlinekeys empty key file")

# ---------------------------------------------------------------------------
# offlinekeys: defaults, limits, output parseability and invalid arguments.
# ---------------------------------------------------------------------------
run_tool(offlinekeys TRUE offline-batch.dat ed25519.keys EDDSA-SHA512-ED25519 30)
assert_output_contains("Signature type: ED25519-SHA512" "offlinekeys signature type")
assert_output_contains("Days: 30" "offlinekeys explicit days")
assert_file_nonempty("${WORK}/offline-batch.dat" "offlinekeys output file")
restrict_secret("${WORK}/offline-batch.dat")

run_tool(keyinfo TRUE -v offline-batch.dat)
assert_output_contains("Offline signature" "keyinfo reads offlinekeys output")
assert_output_contains("Transient Signature Type: ED25519-SHA512" "keyinfo offline transient type")
assert_output_contains("Expires: " "keyinfo offline expiry")

run_tool(offlinekeys TRUE offline-default.dat ed25519.keys)
assert_output_contains("Days: 365" "offlinekeys default days")
restrict_secret("${WORK}/offline-default.dat")

run_tool(offlinekeys TRUE offline-max.dat ed25519.keys EDDSA-SHA512-ED25519 3650)
assert_output_contains("Days: 3650" "offlinekeys maximum days")
restrict_secret("${WORK}/offline-max.dat")

run_tool(offlinekeys FALSE)
assert_output_contains("Usage: offlinekeys" "offlinekeys without arguments")
run_tool(offlinekeys FALSE bad.dat ed25519.keys EDDSA-SHA512-ED25519 0)
assert_output_contains("Days must be 1..3650" "offlinekeys zero days")
run_tool(offlinekeys FALSE bad.dat ed25519.keys EDDSA-SHA512-ED25519 3651)
assert_output_contains("Days must be 1..3650" "offlinekeys upper bound")
run_tool(offlinekeys FALSE bad.dat ed25519.keys EDDSA-SHA512-ED25519 -1)
assert_output_contains("Days must be 1..3650" "offlinekeys negative days")
run_tool(offlinekeys FALSE bad.dat ed25519.keys EDDSA-SHA512-ED25519 abc)
assert_output_contains("Days must be 1..3650" "offlinekeys non-numeric days")
run_tool(offlinekeys FALSE bad.dat ed25519.keys EDDSA-SHA512-ED25519 1x)
assert_output_contains("Days must be 1..3650" "offlinekeys trailing characters")
run_tool(offlinekeys FALSE bad.dat ed25519.keys EDDSA-SHA512-ED25519 999999999999999999999999)
assert_output_contains("Days must be 1..3650" "offlinekeys overflowing days")
run_tool(offlinekeys FALSE bad.dat ed25519.keys NOPE 1)
assert_output_contains("Incorrect signature type" "offlinekeys invalid signature type")
run_tool(offlinekeys FALSE bad.dat missing.keys)
run_tool(offlinekeys FALSE bad.dat malformed.keys)
assert_output_contains("bad keys file format" "offlinekeys malformed key file")
run_tool(offlinekeys FALSE bad.dat empty.keys)
assert_output_contains("bad keys file format" "offlinekeys empty key file")
run_tool(offlinekeys FALSE "${WORK}" ed25519.keys EDDSA-SHA512-ED25519 1)
assert_output_contains("Can't create file" "offlinekeys unwritable output")

# ---------------------------------------------------------------------------
# regaddr / regaddr_3ld / regaddralias: records must verify with verifyhost
# and tampering must be rejected.
# ---------------------------------------------------------------------------
run_tool(keygen TRUE old.keys EDDSA-SHA512-ED25519)
restrict_secret("${WORK}/old.keys")
run_tool(keygen TRUE new.keys EDDSA-SHA512-ED25519)
restrict_secret("${WORK}/new.keys")
run_tool(keyinfo TRUE -d old.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" OLD_DEST)
run_tool(keyinfo TRUE -d new.keys)
string(STRIP "${LAST_TOOL_OUTPUT}" NEW_DEST)

run_tool(regaddr TRUE ed25519.keys example.i2p)
string(STRIP "${LAST_TOOL_OUTPUT}" REGADDR_RECORD)
assert_contains("${REGADDR_RECORD}" "example.i2p=${ED_DEST}#!sig=" "regaddr record")
assert_value_matches("${REGADDR_RECORD}" "^example\\.i2p=[-A-Za-z0-9~]+=*#!sig=[-A-Za-z0-9~]+=*$" "regaddr format")
run_tool(verifyhost TRUE "${REGADDR_RECORD}")
assert_output_contains("Valid destination" "verifyhost accepts regaddr")
string(REPLACE "example.i2p" "evil.i2p" TAMPERED_ADDR "${REGADDR_RECORD}")
run_tool(verifyhost FALSE "${TAMPERED_ADDR}")
assert_output_contains("Invalid destination signature" "verifyhost rejects a tampered regaddr")

run_tool(regaddr FALSE)
assert_output_contains("Usage: regaddr" "regaddr without arguments")
run_tool(regaddr FALSE ed25519.keys)
assert_output_contains("Usage: regaddr" "regaddr missing address")
run_tool(regaddr FALSE missing.keys example.i2p)
assert_output_contains("Failed to load keyfile" "regaddr missing key file")
run_tool(regaddr FALSE malformed.keys example.i2p)
assert_output_contains("Failed to load keyfile" "regaddr malformed key file")

run_tool(regaddr_3ld TRUE step1 new.keys sub.example.i2p)
set(STEP1 "${LAST_TOOL_OUTPUT}")
assert_contains("${STEP1}" "sub.example.i2p=${NEW_DEST}#!action=addsubdomain" "regaddr_3ld step1")
file(WRITE "${WORK}/step1.txt" "${STEP1}")

run_tool(regaddr_3ld TRUE step2 step1.txt old.keys example.i2p)
set(STEP2 "${LAST_TOOL_OUTPUT}")
assert_contains("${STEP2}" "#olddest=${OLD_DEST}" "regaddr_3ld step2 old destination")
assert_contains("${STEP2}" "#oldname=example.i2p" "regaddr_3ld step2 old name")
assert_contains("${STEP2}" "#oldsig=" "regaddr_3ld step2 signature")
file(WRITE "${WORK}/step2.txt" "${STEP2}")

run_tool(regaddr_3ld TRUE step3 step2.txt new.keys)
set(STEP3 "${LAST_TOOL_OUTPUT}")
assert_contains("${STEP3}" "#sig=" "regaddr_3ld step3 signature")
string(STRIP "${STEP3}" STEP3_RECORD)
run_tool(verifyhost TRUE "${STEP3_RECORD}")
assert_output_contains("Valid destination" "verifyhost accepts regaddr_3ld")
string(REPLACE "sub.example.i2p" "evil.example.i2p" TAMPERED_3LD "${STEP3_RECORD}")
run_tool(verifyhost FALSE "${TAMPERED_3LD}")
assert_output_contains("Invalid destination signature" "verifyhost rejects a tampered regaddr_3ld")

run_tool(regaddr_3ld FALSE)
assert_output_contains("Usage:" "regaddr_3ld without arguments")
run_tool(regaddr_3ld FALSE bogus new.keys sub.example.i2p)
assert_output_contains("Usage:" "regaddr_3ld unknown action")
run_tool(regaddr_3ld FALSE step1 new.keys)
assert_output_contains("Usage:" "regaddr_3ld step1 without address")
run_tool(regaddr_3ld FALSE step1 missing.keys sub.example.i2p)
assert_output_contains("Failed to load keyfile" "regaddr_3ld step1 missing keys")
run_tool(regaddr_3ld FALSE step1 malformed.keys sub.example.i2p)
assert_output_contains("Failed to load keyfile" "regaddr_3ld step1 malformed keys")
run_tool(regaddr_3ld FALSE step2 step1.txt old.keys)
assert_output_contains("Usage:" "regaddr_3ld step2 without old name")
run_tool(regaddr_3ld FALSE step2 missing.txt old.keys example.i2p)
assert_output_contains("Failed to read step1 output or keyfile" "regaddr_3ld step2 missing input")
run_tool(regaddr_3ld FALSE step2 step1.txt missing.keys example.i2p)
assert_output_contains("Failed to read step1 output or keyfile" "regaddr_3ld step2 missing keys")
run_tool(regaddr_3ld FALSE step3 step2.txt)
assert_output_contains("Usage:" "regaddr_3ld step3 without keys")
run_tool(regaddr_3ld FALSE step3 missing.txt new.keys)
assert_output_contains("Failed to read step2 output or keyfile" "regaddr_3ld step3 missing input")
run_tool(regaddr_3ld FALSE step3 step2.txt missing.keys)
assert_output_contains("Failed to read step2 output or keyfile" "regaddr_3ld step3 missing keys")

run_tool(regaddralias TRUE old.keys new.keys alias.i2p)
string(STRIP "${LAST_TOOL_OUTPUT}" ALIAS_RECORD)
assert_contains("${ALIAS_RECORD}" "alias.i2p=${NEW_DEST}#!action=adddest#olddest=${OLD_DEST}" "regaddralias destinations")
assert_contains("${ALIAS_RECORD}" "#oldsig=" "regaddralias old signature")
assert_contains("${ALIAS_RECORD}" "#sig=" "regaddralias new signature")
run_tool(verifyhost TRUE "${ALIAS_RECORD}")
assert_output_contains("Valid destination" "verifyhost accepts regaddralias")
string(REPLACE "alias.i2p" "other.i2p" TAMPERED_ALIAS "${ALIAS_RECORD}")
run_tool(verifyhost FALSE "${TAMPERED_ALIAS}")
assert_output_contains("Invalid destination signature" "verifyhost rejects a tampered regaddralias")

run_tool(regaddralias FALSE)
assert_output_contains("Usage: regaddralias" "regaddralias without arguments")
run_tool(regaddralias FALSE old.keys new.keys)
assert_output_contains("Usage: regaddralias" "regaddralias missing address")
run_tool(regaddralias FALSE missing.keys new.keys alias.i2p)
assert_output_contains("Failed to load keyfile" "regaddralias missing old keys")
run_tool(regaddralias FALSE old.keys missing.keys alias.i2p)
assert_output_contains("Failed to load keyfile" "regaddralias missing new keys")
run_tool(regaddralias FALSE malformed.keys new.keys alias.i2p)
assert_output_contains("Failed to load keyfile" "regaddralias malformed old keys")

run_tool(verifyhost FALSE)
assert_output_contains("Usage: verifyhost" "verifyhost without arguments")
run_tool(verifyhost FALSE "not-a-record")
assert_output_contains("Invalid destination record" "verifyhost malformed record")

# ---------------------------------------------------------------------------
# famtool: certificate generation, name-length boundary and invalid options.
# No RouterInfo is available, so the positive sign/verify path is not run.
# ---------------------------------------------------------------------------
run_tool(famtool TRUE -h)
assert_output_contains("usage:" "famtool -h")
assert_output_contains("-g -n" "famtool help generation syntax")

run_tool(famtool FALSE)
assert_output_contains("usage:" "famtool without arguments")
run_tool(famtool FALSE --bogus)
assert_output_contains("usage:" "famtool unknown option")
run_tool(famtool FALSE -g -c x.crt -k x.pem)
assert_output_contains("no family name specified" "famtool generation without family name")

run_tool(famtool TRUE -g -n testfam -c testfam.crt -k testfam.pem)
assert_output_contains("family testfam made" "famtool generation")
assert_file_nonempty("${WORK}/testfam.crt" "famtool certificate file")
assert_file_nonempty("${WORK}/testfam.pem" "famtool private key file")
assert_file_contains_literal("${WORK}/testfam.crt" "BEGIN CERTIFICATE" "famtool certificate PEM")
assert_file_contains_literal("${WORK}/testfam.pem" "PRIVATE KEY" "famtool private key PEM")
restrict_secret("${WORK}/testfam.pem")

string(REPEAT "a" 18 FAMILY_18)
run_tool(famtool TRUE -g -n "${FAMILY_18}" -c family18.crt -k family18.pem)
assert_file_nonempty("${WORK}/family18.crt" "famtool 18-byte family certificate")
restrict_secret("${WORK}/family18.pem")

string(REPEAT "a" 19 FAMILY_19)
run_tool(famtool FALSE -g -n "${FAMILY_19}" -c family19.crt -k family19.pem)
assert_output_contains("family name too long" "famtool rejects a 19-byte family name")

run_tool(famtool FALSE -V -n testfam -c testfam.crt)
assert_output_contains("no router info file specified" "famtool verify without RouterInfo")
run_tool(famtool FALSE -V -n testfam -f bogus.info)
assert_output_contains("no family certificate specified" "famtool verify without certificate")
run_tool(famtool FALSE -V -n testfam -c missing.crt -f bogus.info)
assert_output_contains("invalid certificate" "famtool verify missing certificate")
file(WRITE "${WORK}/bad.pem" "not a certificate\n")
run_tool(famtool FALSE -V -n testfam -c bad.pem -f bogus.info)
assert_output_contains("invalid certificate" "famtool verify malformed certificate")
file(WRITE "${WORK}/bogus.info" "not a RouterInfo\n")
run_tool(famtool FALSE -V -n testfam -c testfam.crt -f bogus.info)
run_tool(famtool FALSE -V -n testfam -c testfam.crt -f missing.info)

run_tool(famtool FALSE -s -n testfam -k testfam.pem -f bogus.info)
assert_output_contains("no router keys file specified" "famtool sign without router keys")
run_tool(famtool FALSE -s -n testfam -i ed25519.keys -f bogus.info)
assert_output_contains("no private key specified" "famtool sign without private key")
run_tool(famtool FALSE -s -n testfam -i ed25519.keys -k testfam.pem)
assert_output_contains("no router info file specified" "famtool sign without RouterInfo")
run_tool(famtool FALSE -s -n testfam -i missing.keys -k testfam.pem -f bogus.info)
run_tool(famtool FALSE -n testfam extra)
assert_output_contains("unexpected argument" "famtool unexpected positional argument")
run_tool(famtool FALSE -g -n testfam -c "${WORK}/missing-dir/testfam.crt" -k "${WORK}/testfam.pem")
assert_output_contains("cannot open" "famtool unwritable certificate path")

# ---------------------------------------------------------------------------
# i2pbase64: text, UTF-8, control bytes, embedded NULs and malformed input.
# ---------------------------------------------------------------------------
file(WRITE "${WORK}/text.bin" "hello i2pd tools\n")
file(READ "${WORK}/text.bin" TEXT_HEX HEX)
run_tool(i2pbase64 TRUE text.bin)
string(STRIP "${LAST_TOOL_OUTPUT}" TEXT_B64)
assert_value_matches("${TEXT_B64}" "^[-A-Za-z0-9~]+=*$" "i2pbase64 text encoding")
file(WRITE "${WORK}/text.b64" "${TEXT_B64}")
run_tool_to_file(i2pbase64 "${WORK}/text.out" TRUE -d text.b64)
assert_file_hex("${WORK}/text.out" "${TEXT_HEX}" "i2pbase64 text round trip")

set(TOOL_STDIN "${WORK}/text.bin")
run_tool(i2pbase64 TRUE)
unset(TOOL_STDIN)
string(STRIP "${LAST_TOOL_OUTPUT}" STDIN_B64)
if(NOT "${STDIN_B64}" STREQUAL "${TEXT_B64}")
  fail("i2pbase64 stdin encoding differs from file encoding")
endif()
file(WRITE "${WORK}/stdin.b64" "${TEXT_B64}")
set(TOOL_STDIN "${WORK}/stdin.b64")
run_tool(i2pbase64 TRUE -d)
unset(TOOL_STDIN)
assert_output_contains("hello i2pd tools" "i2pbase64 stdin decoding")

file(WRITE "${WORK}/unicode.txt" "line one\nlínea dos\n日本語 ✓\n")
file(READ "${WORK}/unicode.txt" UNICODE_HEX HEX)
run_tool(i2pbase64 TRUE unicode.txt)
string(STRIP "${LAST_TOOL_OUTPUT}" UNICODE_B64)
file(WRITE "${WORK}/unicode.b64" "${UNICODE_B64}")
run_tool_to_file(i2pbase64 "${WORK}/unicode.out" TRUE -d unicode.b64)
assert_file_hex("${WORK}/unicode.out" "${UNICODE_HEX}" "i2pbase64 UTF-8 round trip")

# Control bytes exercise the binary path; NUL is covered separately below
# because CMake cannot reliably carry a NUL inside a script string.
string(ASCII 1 2 3 7 8 11 12 31 127 BINARY_TEXT)
file(WRITE "${WORK}/binary.bin" "${BINARY_TEXT}")
file(READ "${WORK}/binary.bin" BINARY_HEX HEX)
run_tool(i2pbase64 TRUE binary.bin)
string(STRIP "${LAST_TOOL_OUTPUT}" BINARY_B64)
file(WRITE "${WORK}/binary.b64" "${BINARY_B64}")
run_tool_to_file(i2pbase64 "${WORK}/binary.out" TRUE -d binary.b64)
assert_file_hex("${WORK}/binary.out" "${BINARY_HEX}" "i2pbase64 binary round trip")

file(WRITE "${WORK}/nul.b64" "AA==")
run_tool_to_file(i2pbase64 "${WORK}/nul.bin" TRUE -d nul.b64)
assert_file_hex("${WORK}/nul.bin" "00" "i2pbase64 decodes NUL")
run_tool(i2pbase64 TRUE nul.bin)
assert_stripped_output_equals("AA==" "i2pbase64 encodes NUL")

file(WRITE "${WORK}/spaced.b64" "aGVs\n bG8g\td29y bGQ=\n")
file(WRITE "${WORK}/spaced.expected" "hello world")
file(READ "${WORK}/spaced.expected" SPACED_HEX HEX)
run_tool_to_file(i2pbase64 "${WORK}/spaced.out" TRUE -d spaced.b64)
assert_file_hex("${WORK}/spaced.out" "${SPACED_HEX}" "i2pbase64 ignores ASCII whitespace")

# The I2P alphabet uses '-' and '~' where standard base64 uses '+' and '/'.
file(WRITE "${WORK}/i2p-dash.b64" "-A==")
run_tool_to_file(i2pbase64 "${WORK}/i2p-dash.bin" TRUE -d i2p-dash.b64)
assert_file_hex("${WORK}/i2p-dash.bin" "f8" "i2pbase64 decodes the I2P '-' digit")
file(WRITE "${WORK}/i2p-tilde.b64" "~w==")
run_tool_to_file(i2pbase64 "${WORK}/i2p-tilde.bin" TRUE -d i2p-tilde.b64)
assert_file_hex("${WORK}/i2p-tilde.bin" "ff" "i2pbase64 decodes the I2P '~' digit")
file(WRITE "${WORK}/standard-alphabet.b64" "++==")
run_tool(i2pbase64 FALSE -d standard-alphabet.b64)
assert_output_contains("invalid base64 input" "i2pbase64 rejects the standard '+' digit")

file(WRITE "${WORK}/whitespace.b64" " \n\r\t\n")
run_tool(i2pbase64 TRUE -d whitespace.b64)
if(NOT "${LAST_TOOL_OUTPUT}" STREQUAL "")
  fail("i2pbase64 produced output for whitespace-only input")
endif()
file(WRITE "${WORK}/empty.bin" "")
run_tool(i2pbase64 TRUE empty.bin)
if(NOT "${LAST_TOOL_OUTPUT}" STREQUAL "")
  fail("i2pbase64 produced output for an empty file")
endif()

run_tool(i2pbase64 TRUE -h)
assert_output_contains("usage:" "i2pbase64 -h")
file(WRITE "${WORK}/invalid.b64" "AA!A")
run_tool(i2pbase64 FALSE -d invalid.b64)
assert_output_contains("invalid base64 input" "i2pbase64 rejects invalid alphabet")
file(WRITE "${WORK}/truncated.b64" "AAA")
run_tool(i2pbase64 FALSE -d truncated.b64)
assert_output_contains("truncated base64 input" "i2pbase64 rejects a partial quartet")
file(WRITE "${WORK}/leading-padding.b64" "=AAA")
run_tool(i2pbase64 FALSE -d leading-padding.b64)
assert_output_contains("invalid base64 input" "i2pbase64 rejects leading padding")
file(WRITE "${WORK}/after-padding.b64" "AA==AAAA")
run_tool(i2pbase64 FALSE -d after-padding.b64)
assert_output_contains("invalid base64 input" "i2pbase64 rejects data after padding")
run_tool(i2pbase64 FALSE missing.bin)
run_tool(i2pbase64 FALSE text.bin extra.bin)
assert_output_contains("usage:" "i2pbase64 rejects extra arguments")
run_tool(i2pbase64 FALSE -x text.bin)
assert_output_contains("usage:" "i2pbase64 rejects unknown options")

# ---------------------------------------------------------------------------
# vain: bounds and invalid arguments only. A valid search is never started.
# ---------------------------------------------------------------------------
run_tool(vain TRUE)
assert_output_contains("vain pattern" "vain without arguments prints usage")
run_tool(vain TRUE --help)
assert_output_contains("--multiplymode" "vain --help multiply mode")
assert_output_contains("--threads" "vain --help threads")
assert_output_contains("--signature" "vain --help signature")

run_tool(vain FALSE -t 0 example)
assert_output_contains("Invalid thread count" "vain rejects zero threads")
run_tool(vain FALSE -t 1025 example)
assert_output_contains("Invalid thread count" "vain rejects more than 1024 threads")
run_tool(vain FALSE -t -1 example)
assert_output_contains("Invalid thread count" "vain rejects negative threads")
run_tool(vain FALSE -t abc example)
assert_output_contains("Invalid thread count" "vain rejects non-numeric threads")
run_tool(vain FALSE -t 4294967296 example)
assert_output_contains("Invalid thread count" "vain rejects overflowing threads")
run_tool(vain FALSE --threads=1025 example)
assert_output_contains("Invalid thread count" "vain rejects overflowing long thread option")
run_tool(vain FALSE -t)
assert_output_contains("Undefined argument" "vain missing -t argument")

run_tool(vain FALSE -s NOPE example)
assert_output_contains("Invalid signature type" "vain rejects an unknown signature name")
run_tool(vain FALSE -s ECDSA-P256 example)
assert_output_contains("only ED25519-SHA512" "vain rejects an unsupported signature type")

run_tool(vain FALSE -r "(")
assert_output_contains("Invalid regular expression" "vain rejects an invalid regex")
run_tool(vain FALSE -r "[")
assert_output_contains("Invalid regular expression" "vain rejects an unterminated regex class")
run_tool(vain FALSE -r)
assert_output_contains("vain pattern" "vain -r without a pattern prints usage")

run_tool(vain FALSE "NOT-BASE32")
assert_output_contains("Not correct prefix" "vain rejects an invalid base32 prefix")
run_tool(vain FALSE "abc0")
assert_output_contains("Not correct prefix" "vain rejects base32 digit 0")
string(REPEAT "a" 53 LONG_PREFIX)
run_tool(vain FALSE "${LONG_PREFIX}")
assert_output_contains("Not correct prefix" "vain rejects a prefix longer than 52 characters")

run_tool(vain FALSE -m)
assert_output_contains("vain pattern" "vain multiply mode without a pattern")
run_tool(vain FALSE -o)
assert_output_contains("Undefined argument" "vain missing -o argument")
run_tool(vain FALSE --bogus example)

# ---------------------------------------------------------------------------
# autoconf_i2pd: deterministic non-interactive transcript. It writes a
# configuration file below WORK and never touches the real data directory.
# ---------------------------------------------------------------------------
file(WRITE "${WORK}/autoconf.in"
  "en\n1\n"
  "n\nn\nn\nn\nn\n"
  "n\ny\n"
  "n\nn\n"
  "-\n"
  "n\nn\n"
  "-\n-\n"
  "n\nn\n-\n-\n"
  "n\n-\n-\n"
  "-\n"
  "autoconf.conf\n")
set(TOOL_STDIN "${WORK}/autoconf.in")
run_tool(autoconf_i2pd TRUE)
unset(TOOL_STDIN)
assert_file_nonempty("${WORK}/autoconf.conf" "autoconf output")
assert_file_contains_literal("${WORK}/autoconf.conf" "[ntcp2]" "autoconf ntcp2 section")
assert_file_contains_literal("${WORK}/autoconf.conf" "[ssu2]" "autoconf ssu2 section")
assert_file_contains_literal("${WORK}/autoconf.conf" "[http]" "autoconf http section")
assert_file_contains_literal("${WORK}/autoconf.conf" "ipv4=true" "autoconf ipv4 answer")

file(REMOVE_RECURSE "${WORK}")
message(STATUS "tools-regressions: all checks passed")
