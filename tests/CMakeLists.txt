# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: CMakeLists.txt.
#
# Registers the C++ tests plus interoperability and static build-system checks
# as CTest targets, labelled by unit, torrent, IPC, TUI and security groups.
#

enable_testing()

function(i2pd_add_test name)
  add_executable(${name} "${name}.cpp")
  target_compile_features(${name} PRIVATE cxx_std_23)
  if(name STREQUAL "test-privsep-protocol")
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/Channel.cpp")
  elseif(name STREQUAL "test-privsep-log")
    target_sources(${name} PRIVATE
      "${PROJECT_ROOT}/privsep/Channel.cpp"
      "${PROJECT_ROOT}/privsep/LinuxSandbox.cpp"
      "${PROJECT_ROOT}/privsep/OpenBSDSandbox.cpp"
      "${PROJECT_ROOT}/privsep/LogWorker.cpp")
  elseif(name STREQUAL "test-privsep-linux-ipc")
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/Channel.cpp")
  elseif(name STREQUAL "test-privsep-linux-sandbox")
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/LinuxSandbox.cpp")
  elseif(name STREQUAL "test-privsep-openbsd-policy")
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/OpenBSDSandbox.cpp")
  elseif(name STREQUAL "test-privsep-openbsd-imsg")
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/OpenBSDImsg.cpp")
    if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
      # OpenBSD libutil provides imsgbuf_init/imsg_compose/imsgbuf_get.
      target_link_libraries(${name} PRIVATE util)
    endif()
  elseif(name STREQUAL "test-tui-terminal")
    # Header-only shared key/attribute contract; no ncurses or console init.
    target_include_directories(${name} PRIVATE "${PROJECT_ROOT}/tui")
  elseif(name STREQUAL "test-tui-pipe-name")
    # The pipe-name helper is portable; keep the test runnable on Unix as well
    # as Windows.
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/tui/TuiWin32Ipc.cpp")
    target_include_directories(${name} PRIVATE "${PROJECT_ROOT}/tui")
  elseif(name STREQUAL "test-tui-win32-pipe")
    # Exercises the Win32 named-pipe and DACL primitives used by the Windows
    # TUI transport. It does not validate a running console or kernel ACLs
    # across user accounts.
    target_link_libraries(${name} PRIVATE advapi32)
  elseif(name STREQUAL "test-privsep-windows-ipc")
    # Worker message-mode pipe, protected DACL, peer token inspection,
    # restricted-token and Job Object policy. No cross-user/service test.
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/WindowsIpc.cpp")
    target_compile_definitions(${name} PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(${name} PRIVATE advapi32)
  elseif(name STREQUAL "test-tui-ipc-regression")
    # The real local transport and shared dispatch are not part of libi2pd.
    target_sources(${name} PRIVATE
      "${PROJECT_ROOT}/daemon/TuiServer.cpp"
      "${PROJECT_ROOT}/daemon/TuiServerCommon.cpp"
      "${PROJECT_ROOT}/tui/TuiClient.cpp")
    target_include_directories(${name} PRIVATE
      "${PROJECT_ROOT}/daemon"
      "${PROJECT_ROOT}/tui")
  endif()
  target_include_directories(${name} PRIVATE "${PROJECT_ROOT}" "${PROJECT_ROOT}/tools")
  target_link_libraries(${name} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
  if(WIN32)
    # Keep the WSA extension symbols explicit for MinGW/Clang64 executables.
    target_link_libraries(${name} PRIVATE mswsock ws2_32)
  endif()
  if(MSVC)
    target_compile_options(${name} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${name} PRIVATE -UNDEBUG)
  endif()
  i2pd_enable_sanitizers(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

set(I2PD_TESTS
  test-http-merge_chunked
  test-http-req
  test-http-res
  test-http-url
  test-http-url_decode
  test-base-64
  test-gost
  test-gost-sig
  test-aeadchacha20poly1305
  test-blinding
  test-elligator
  test-eddsa
  test-siphash
  test-aes
  test-tui-protocol
  test-tui-terminal
  test-tui-pipe-name
  test-routerinfo-firewall
  test-su3-bounds
  test-ntcp2-session-confirmed-payload
  test-ntcp2-handshake-bounds
  test-datagram-options
  test-sam-datagram-framing
  test-streaming-buffer
  test-log-lifecycle
  test-torrent-bencode
  test-torrents-security
  test-torrents-concurrency
  test-blinding-b33-strict
  test-crypto-audit-bounds
  test-reseed-zip-bounds
  test-ssu2-routerinfo-bounds
  test-routerinfo-java213-parsing
  test-routerinfo-update-transaction
  test-routercontext-update-race
  test-routercontext-service-lifecycle
  test-routercontext-static-keys
  test-kad-dht
  test-addressbook
  test-ssu2-address-negotiation
  test-ssu2-fragment-ordering
  test-ssu2-session-fragment-bounds
  test-ssu2-control-plane
  test-ssu2-preauth
  test-crypto-wire-validation
  test-crypto-x25519-noise-elligator-mlkem-audit
  test-eciesx25519-ratchet-session
  test-eciesx25519-ratchet-tagsets
  test-signature-identity-hardening
  test-blinding-b33-hardening
  test-gost-hash-streaming
  test-leaseset-netdb-2.13-freshness
  test-leaseset2-java213-regression
  test-leaseset2-multikey-negotiation-2026
  test-netdb-lookup-wire-bounds
  test-netdb-dsrm-correlation
  test-netdb-store-lookup-hardening
  test-protocol-bounds
  test-http-proxy-limits
  test-i2cp-session-lifecycle
  test-clientcontext-registry
  test-sam-stream-lifecycle
  test-tunnelpool-zero-hop
  test-tunnelpool-detach-race
  test-tunnels-restart
  test-timestamp-offset
  test-ntp-timesync-lifecycle
)

if(UNIX)
  # Portable Unix IPC/protocol tests. Platform-specific sandbox/transport
  # tests are registered only below; the source files also contain stubs, but
  # the Linux sandbox test includes <linux/capability.h> before its guard and
  # must never be added to an OpenBSD build.
  list(APPEND I2PD_TESTS
    test-privsep-protocol
    test-privsep-log
    test-tui-ipc-regression)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND I2PD_TESTS
    test-privsep-linux-ipc
    test-privsep-linux-sandbox)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  list(APPEND I2PD_TESTS
    test-privsep-openbsd-policy
    test-privsep-openbsd-imsg)
endif()

if(WIN32)
  # Windows-only transport primitives. The test is compiled but no live
  # console, service account or cross-user ACL check is performed.
  list(APPEND I2PD_TESTS test-tui-win32-pipe test-privsep-windows-ipc)
endif()

foreach(test IN LISTS I2PD_TESTS)
  i2pd_add_test(${test})
  set_property(TEST ${test} APPEND PROPERTY LABELS unit)
  if(test MATCHES "^test-torrent")
    set_property(TEST ${test} APPEND PROPERTY LABELS torrent)
  endif()
  if(test MATCHES "privsep|tui-ipc|tui-pipe|tui-win32")
    set_property(TEST ${test} APPEND PROPERTY LABELS ipc)
  endif()
  if(test MATCHES "^test-tui")
    set_property(TEST ${test} APPEND PROPERTY LABELS tui)
  endif()
  if(test MATCHES "crypto|blinding|su3|reseed|gost|signature|ecies|protocol-bounds|routerinfo|ssu2|ntcp2|netdb|leaseset|torrents-security|race|concurrency|sandbox|policy|imsg")
    set_property(TEST ${test} APPEND PROPERTY LABELS security)
  endif()
  if(test MATCHES "bounds|audit|hardening|security|race|concurrency|lifecycle|sandbox|policy")
    set_property(TEST ${test} APPEND PROPERTY LABELS memory)
  endif()
  if(test MATCHES "^test-(http|base-64|gost|aead|blinding|elligator|eddsa|siphash|aes|ntcp2|ssu2|protocol|netdb|leaseset|routerinfo|routercontext|kad|addressbook|datagram|streaming|timestamp|ntp|i2cp|sam|clientcontext|log|tunnel)")
    set_property(TEST ${test} APPEND PROPERTY LABELS i2p)
  endif()
  if(test MATCHES "leaseset|routerinfo-java213|torrents-(engine-interop|dht-interop|comments|magnet)")
    set_property(TEST ${test} APPEND PROPERTY LABELS interop)
  endif()
  if(test MATCHES "privsep-(linux|openbsd)|tui-win32|privsep-windows")
    set_property(TEST ${test} APPEND PROPERTY LABELS platform)
  endif()
  if(test MATCHES "routerinfo-firewall")
    set_property(TEST ${test} APPEND PROPERTY LABELS tools)
  endif()
endforeach()

# Static build-system policy check. It does not invoke a compiler, a build
# tool or a project binary.
add_test(NAME build-system-static
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check-build-system.cmake")
set_tests_properties(build-system-static PROPERTIES LABELS "ci;static")

# Shell-level checks that only need the built router binary.
if(TARGET i2pd)
  add_test(NAME i2pd-version COMMAND i2pd --version)
  set_tests_properties(i2pd-version PROPERTIES LABELS "ci;smoke")
endif()

# Interoperability fixtures and documentation/provenance checks. These are
# static and never clone or execute the Java reference.
find_package(Perl)
if(PERL_EXECUTABLE)
  add_test(NAME interop-static
    COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/interop/check-vectors.pl")
  set_tests_properties(interop-static PROPERTIES
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    LABELS "interop;static"
    TIMEOUT 60)

  add_test(NAME perl-doc-links
    COMMAND "${PERL_EXECUTABLE}" "${PROJECT_ROOT}/tools/scripts/check-doc-links.pl" docs)
  set_tests_properties(perl-doc-links PROPERTIES
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    LABELS "perl;static"
    TIMEOUT 60)

  # Android static policy tests are compile-free and can run on every host
  # with Perl. Gradle/APK/Lint/device execution remains in the Android job.
  add_test(NAME android-english-only
    COMMAND "${PERL_EXECUTABLE}" "${PROJECT_ROOT}/android/tools/check-english-only.pl")
  set_tests_properties(android-english-only PROPERTIES
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    LABELS "android;perl;static"
    TIMEOUT 60)
endif()

if(UNIX)
  find_program(SH_EXECUTABLE sh)
  if(SH_EXECUTABLE AND EXISTS "${PROJECT_ROOT}/android/tools/java25-probe/check-config.sh")
    add_test(NAME android-config-static
      COMMAND "${SH_EXECUTABLE}" "${PROJECT_ROOT}/android/tools/java25-probe/check-config.sh")
    set_tests_properties(android-config-static PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "android;static"
      TIMEOUT 30)
  endif()
endif()

# Optional pinned-Java provenance check. The reference is deliberately kept
# outside the source tree; set -DI2PD_INTEROP_REFERENCE=/path/to/java-checkout
# at configure time to register it.
set(I2PD_INTEROP_REFERENCE "" CACHE PATH "Read-only pinned I2P Java checkout")
if(I2PD_INTEROP_REFERENCE)
  add_test(NAME interop-reference
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/interop/check-reference.sh")
  set_tests_properties(interop-reference PROPERTIES
    ENVIRONMENT "I2P_JAVA_REFERENCE=${I2PD_INTEROP_REFERENCE}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    LABELS "interop;reference"
    TIMEOUT 60)

  if(PERL_EXECUTABLE)
    add_test(NAME interop-reference-vectors
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/interop/check-vectors.pl"
        --reference "${I2PD_INTEROP_REFERENCE}" --require-reference)
    set_tests_properties(interop-reference-vectors PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "interop;reference"
      TIMEOUT 120)
  endif()
endif()

# Optional libFuzzer targets. They are not built by default; when enabled,
# one-input smoke entries are registered under the fuzz label while the manual
# workflow runs each target with a corpus and a wall-clock budget.
option(WITH_FUZZERS "Build libFuzzer targets (Clang)" OFF)
if(WITH_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WITH_FUZZERS requires Clang")
  endif()
  foreach(fuzzer fuzz-tui-protocol fuzz-http-parser fuzz-protocol-bounds fuzz-log-record fuzz-privsep-protocol fuzz-torrents-parsers fuzz-base-blinding fuzz-signature-der)
    add_executable(${fuzzer} "fuzz/${fuzzer}.cpp")
    target_compile_features(${fuzzer} PRIVATE cxx_std_23)
    target_link_libraries(${fuzzer} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
    target_compile_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
    # Optional, non-default CTest smoke entry. The manual Fuzzing workflow
    # runs each target for a bounded wall-clock budget with a corpus; this
    # one-input check makes `ctest -L fuzz` useful without making fuzzing part
    # of the normal test build.
    add_test(NAME fuzz-smoke-${fuzzer}
      COMMAND ${fuzzer} -runs=1 -timeout=10)
    set_tests_properties(fuzz-smoke-${fuzzer} PROPERTIES
      LABELS "fuzz;smoke"
      TIMEOUT 30)
  endforeach()
endif()
