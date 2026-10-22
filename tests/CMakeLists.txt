# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: CMakeLists.txt.
#
# Registers the C++ tests plus interoperability and static build-system checks
# as CTest targets, labelled by unit, torrent, IPC, TUI and security groups.
#


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
    target_link_libraries(${name} PRIVATE advapi32 bcrypt)
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
  set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

set(I2PD_TESTS
  test-http-merge_chunked
  test-http-req
  test-http-res
  test-http-url
  test-http-url_decode
  test-i2ptunnel-http
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
  test-sam-control-framing
  test-streaming-buffer
  test-log-lifecycle
  test-torrent-bencode
  test-torrents-comments
  test-torrents-creator
  test-torrent-payload-storage
  test-torrents-state
  test-torrents-priorities
  test-torrents-magnet
  test-torrents-engine-interop
  test-torrents-dht-interop
  test-torrents-security
  test-torrents-verify-parity
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
  test-routercontext-port-range
  test-router-profile-race
  test-router-profile-storage
  test-netdb-stop-admission
  test-datagram-receiver-lifecycle
  test-android-daemon-lifecycle
  test-daemon-init-rollback
  test-tunnel-dsm-policy
  test-tunnelconfig-ownership
  test-transport-connector
  test-modern
  test-address-format
  test-util-copy-string
  test-rng-seed
  test-runnableservice-lifecycle
  test-i18n-format
  test-i18n-locale
  test-socks-address
  test-kad-dht
  test-addressbook
  test-addressbook-admin
  test-addressbook-case
  test-ssu2-address-negotiation
  test-ssu2-fragment-ordering
  test-ssu2-session-fragment-bounds
  test-ssu2-control-plane
  test-ssu2-preauth
  test-crypto-wire-validation
  test-crypto-x25519-noise-elligator-mlkem-audit
  test-openssl-capabilities
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
  test-i2cp-routing-session-key
  test-clientcontext-registry
  test-sam-stream-lifecycle
  test-tunnelpool-zero-hop
  test-tunnelpool-detach-race
  test-tunnels-restart
  test-timestamp-offset
  test-ntp-timesync-lifecycle
)

if(WITH_TORRENTS)
  list(APPEND I2PD_TESTS test-boost-linkage)
endif()

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
    test-torrent-storage-worker
    test-torrent-storage-lifecycle
    test-torrents-dht-persistence
    test-router-storage
    test-privsep-linux-ipc
    test-privsep-linux-sandbox)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  list(APPEND I2PD_TESTS
    test-privsep-openbsd-policy
    test-privsep-openbsd-imsg
    test-torrent-storage-openbsd)
endif()

if(WIN32)
  # Windows-only transport primitives. The test is compiled but no live
  # console, service account or cross-user ACL check is performed.
  list(APPEND I2PD_TESTS test-tui-win32-pipe test-privsep-windows-ipc)
endif()

foreach(test IN LISTS I2PD_TESTS)
  i2pd_add_test(${test})
  if(test STREQUAL "test-boost-linkage")
    set_property(TEST ${test} APPEND PROPERTY LABELS "ci;torrent")
  endif()
  if(test STREQUAL "test-torrent-storage-worker" OR test STREQUAL "test-torrent-storage-openbsd" OR test STREQUAL "test-torrent-storage-lifecycle" OR test STREQUAL "test-torrents-dht-persistence" OR test STREQUAL "test-router-storage")
    target_link_libraries(${test} PRIVATE i2pd_privsep)
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    set_property(TEST ${test} APPEND PROPERTY LABELS "ipc;security;platform")
  endif()
  if(test STREQUAL "test-privsep-linux-sandbox")
    set_tests_properties(${test} PROPERTIES SKIP_RETURN_CODE 77)
  endif()
  set_property(TEST ${test} APPEND PROPERTY LABELS unit)
  if(test STREQUAL "test-android-daemon-lifecycle")
    set_property(TEST ${test} APPEND PROPERTY LABELS "android;security")
  endif()
  if(test STREQUAL "test-daemon-init-rollback")
    set_property(TEST ${test} APPEND PROPERTY LABELS "android;security;memory")
  endif()
  if(test STREQUAL "test-router-profile-race")
    set_property(TEST ${test} APPEND PROPERTY LABELS i2p)
  endif()
  if(test STREQUAL "test-datagram-receiver-lifecycle")
    set_property(TEST ${test} APPEND PROPERTY LABELS "i2p;security;memory")
  endif()
  if(test STREQUAL "test-netdb-stop-admission")
    set_property(TEST ${test} APPEND PROPERTY LABELS "i2p;security;memory")
  endif()
  if(test STREQUAL "test-router-profile-storage")
    set_property(TEST ${test} APPEND PROPERTY LABELS "i2p;security;memory")
  endif()
  if(test MATCHES "^test-torrent")
    set_property(TEST ${test} APPEND PROPERTY LABELS torrent)
  endif()
  if(test MATCHES "privsep|tui-ipc|tui-pipe|tui-win32")
    set_property(TEST ${test} APPEND PROPERTY LABELS ipc)
  endif()
  if(test MATCHES "^test-torrent" AND NOT WITH_TORRENTS)
    set_tests_properties(${test} PROPERTIES DISABLED TRUE)
  endif()
  if(test MATCHES "^test-tui")
    set_property(TEST ${test} APPEND PROPERTY LABELS tui)
  endif()
  if(test STREQUAL "test-routercontext-port-range"
      OR test STREQUAL "test-i2ptunnel-http"
      OR test STREQUAL "test-addressbook-admin"
      OR test STREQUAL "test-addressbook-case"
      OR test STREQUAL "test-tunnelconfig-ownership"
      OR test STREQUAL "test-runnableservice-lifecycle"
      OR test STREQUAL "test-address-format"
      OR test STREQUAL "test-util-copy-string"
      OR test STREQUAL "test-rng-seed"
      OR test STREQUAL "test-i18n-format"
      OR test STREQUAL "test-socks-address"
      OR test STREQUAL "test-openssl-capabilities"
      OR test STREQUAL "test-http-proxy-limits")
    set_property(TEST ${test} APPEND PROPERTY LABELS security)
  endif()
  if(test STREQUAL "test-i2ptunnel-http"
      OR test STREQUAL "test-addressbook-admin"
      OR test STREQUAL "test-addressbook-case"
      OR test STREQUAL "test-tunnelconfig-ownership"
      OR test STREQUAL "test-address-format"
      OR test STREQUAL "test-util-copy-string"
      OR test STREQUAL "test-rng-seed"
      OR test STREQUAL "test-i18n-format"
      OR test STREQUAL "test-socks-address"
      OR test STREQUAL "test-openssl-capabilities"
      OR test STREQUAL "test-http-proxy-limits")
    set_property(TEST ${test} APPEND PROPERTY LABELS memory)
  endif()
  if(test MATCHES "crypto|aead|aes|siphash|eddsa|elligator|blinding|su3|reseed|gost|signature|ecies|openssl|http-proxy-limits|protocol-bounds|routerinfo|ssu2|ntcp2|netdb|leaseset|torrents-security|torrents-verify-parity|i2cp-routing-session|sam-control|race|concurrency|sandbox|policy|imsg")
    set_property(TEST ${test} APPEND PROPERTY LABELS security)
  endif()
  if(test MATCHES "bounds|audit|hardening|security|verify-parity|race|concurrency|lifecycle|sandbox|policy")
    set_property(TEST ${test} APPEND PROPERTY LABELS memory)
  endif()
  if(test MATCHES "^test-(http|base-64|gost|aead|blinding|elligator|eddsa|siphash|aes|crypto|ecies|signature|su3|reseed|openssl|ntcp2|ssu2|protocol|netdb|leaseset|routerinfo|routercontext|kad|addressbook|datagram|streaming|timestamp|ntp|i2cp|i2ptunnel|sam|clientcontext|log|tunnel|transport)")
    set_property(TEST ${test} APPEND PROPERTY LABELS i2p)
  endif()
  if(test MATCHES "leaseset|routerinfo-java213|torrents-(engine-interop|dht-interop|comments|magnet|creator|verify-parity)")
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
set_tests_properties(build-system-static PROPERTIES
  LABELS "ci;static"
  TIMEOUT 60)

# Compile-free modern-language policy/availability check. It reads AGENTS.md,
# CMake, Gradle, documentation and the modernization seam headers only.
add_test(NAME language-modernization-static
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check-language-modernization.cmake")
set_tests_properties(language-modernization-static PROPERTIES
  LABELS "ci;docs;static"
  TIMEOUT 30)

# Compile-free native/Android translation contract check.
add_test(NAME i18n-static
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check-i18n.cmake")
set_tests_properties(i18n-static PROPERTIES
  LABELS "ci;docs;static"
  TIMEOUT 30)

# Static init-system service contract check. It reads the canonical service
# sources and never invokes a shell, service manager or compiler.
add_test(NAME services-static
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check-services.cmake")
set_tests_properties(services-static PROPERTIES
  LABELS "services;static"
  TIMEOUT 60)

# Compile-free packaging/install-contract check. It reads the POSIX Makefile,
# RPM/Docker/static packaging files and CMake install rules only.
add_test(NAME packaging-static
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check-packaging.cmake")
set_tests_properties(packaging-static PROPERTIES
  LABELS "packaging;install;static"
  TIMEOUT 30)

# Static Agent Skills contract check. It reads the skill frontmatter,
# references and required sections; it does not execute a skill or a workflow.
add_test(NAME agent-skills-static
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_ROOT=${PROJECT_ROOT}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check-agent-skills.cmake")
set_tests_properties(agent-skills-static PROPERTIES
  LABELS "ci;docs;static"
  TIMEOUT 60)

# Shell-level checks that only need the built router binary.
if(TARGET i2pd)
  add_test(NAME i2pd-version COMMAND i2pd --version)
  set_tests_properties(i2pd-version PROPERTIES
    LABELS "ci;smoke"
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "i2pd")
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
    LABELS "docs;perl;static"
    TIMEOUT 60)

  add_test(NAME docs-nav-static
    COMMAND "${PERL_EXECUTABLE}" "${PROJECT_ROOT}/tools/scripts/check-doc-nav.pl" "${PROJECT_ROOT}")
  set_tests_properties(docs-nav-static PROPERTIES
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    LABELS "docs;perl;static"
    TIMEOUT 60)

  # Android static policy tests are compile-free and can run on every host
  # with Perl. Gradle/APK/Lint/device execution remains in the Android job.
  add_test(NAME android-locale-policy
    COMMAND "${PERL_EXECUTABLE}" "${PROJECT_ROOT}/android/tools/check-android-locales.pl")
  set_tests_properties(android-locale-policy PROPERTIES
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

# Current comparison target and historical fixture provenance are separate
# read-only external checkouts. Do not apply old source-line citations to a
# newer release merely by changing its reference pin.
set(I2PD_INTEROP_REFERENCE "" CACHE PATH "Read-only I2P Java 2.14.0 comparison checkout")
if(I2PD_INTEROP_REFERENCE)
  add_test(NAME interop-reference
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/interop/check-reference.sh")
  set_tests_properties(interop-reference PROPERTIES
    ENVIRONMENT "I2P_JAVA_REFERENCE=${I2PD_INTEROP_REFERENCE}"
    WORKING_DIRECTORY "${PROJECT_ROOT}"
    LABELS "interop;reference"
    TIMEOUT 60)

endif()

set(I2PD_INTEROP_FIXTURE_REFERENCE "" CACHE PATH "Read-only I2P Java 2.13.0 fixture provenance checkout")
if(I2PD_INTEROP_FIXTURE_REFERENCE)
  if(PERL_EXECUTABLE)
    add_test(NAME interop-reference-vectors
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/interop/check-vectors.pl"
        --reference "${I2PD_INTEROP_FIXTURE_REFERENCE}" --require-reference)
    set_tests_properties(interop-reference-vectors PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "interop;reference"
      TIMEOUT 120)
  endif()
endif()

# Optional libFuzzer targets. They are not built by default; when enabled,
# one-input smoke entries are registered under the fuzz label while the manual
# workflow runs each target with a corpus and a wall-clock budget.
if(WITH_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WITH_FUZZERS requires Clang")
  endif()
  foreach(fuzzer fuzz-tui-protocol fuzz-http-parser fuzz-protocol-bounds fuzz-log-record fuzz-privsep-protocol fuzz-torrents-parsers fuzz-base-blinding fuzz-signature-der)
    add_executable(${fuzzer} "fuzz/${fuzzer}.cpp" "fuzz-initialize.cpp")
    target_compile_features(${fuzzer} PRIVATE cxx_std_23)
    # Some fuzzers include privsep/, daemon/ and other project-root headers.
    target_include_directories(${fuzzer} PRIVATE "${PROJECT_ROOT}")
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
