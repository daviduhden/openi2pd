# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: CMakeLists.txt.
#
# Defines the auxiliary native tool executables and links them against
# libi2pd (autoconf_i2pd also against libi2pdlang). It installs i2pdctl and,
# under BUILD_TESTING with Perl, registers syntax, portability, branding and
# install-layout tests.
#

set(I2PD_TOOLS
  autoconf_i2pd
  b33address
  b33offlinekeys
  famtool
  i2pbase64
  keygen
  keyinfo
  offlinekeys
  regaddr
  regaddr_3ld
  regaddralias
  routerinfo
  vain
  verifyhost
  x25519
)
string(REPLACE ";" "," I2PD_TOOLS_INVENTORY "${I2PD_TOOLS}")

# The getopt compatibility layer is only needed where the platform lacks
# <getopt.h>; on POSIX the translation unit is empty and libc provides it.
set(I2PD_TOOLS_GETOPT_SOURCE)
if(WIN32)
  set(I2PD_TOOLS_GETOPT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/common/getopt.cpp")
endif()

foreach(tool IN LISTS I2PD_TOOLS)
  add_executable(${tool}
    "${CMAKE_CURRENT_SOURCE_DIR}/${tool}.cpp"
    ${I2PD_TOOLS_GETOPT_SOURCE})
  target_compile_features(${tool} PRIVATE cxx_std_23)
  # libi2pd publishes its own include directories. Only the tool-local
  # "common/..." headers need an extra search root.
  target_include_directories(${tool} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  # No tool includes a libi2pdclient header; linking it pulled Boost.JSON/URL
  # and the client graph into every auxiliary executable.
  target_link_libraries(${tool} PRIVATE libi2pd i2pd_warnings Threads::Threads)
  if(tool STREQUAL "autoconf_i2pd")
    # The interactive generator is the only translated tool.
    target_link_libraries(${tool} PRIVATE libi2pdlang)
  endif()
  i2pd_enable_sanitizers(${tool})
endforeach()

install(TARGETS ${I2PD_TOOLS}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tools)

# i2pdctl is a Perl client for the retained I2PControl service. It uses only
# core Perl modules plus the system openssl binary for the self-signed HTTPS
# fallback; it is not a dependency of the daemon. Runtime requirements are a
# Perl 5 interpreter and either IO::Socket::SSL or the openssl command.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/i2pdctl"
  DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tools)
# Every imported tool binary must travel with its BSD-3 attribution. When the
# documentation component is installed it owns the same file, so keep exactly
# one install rule to avoid two components racing on one destination.
if(NOT I2PD_INSTALL_DOCS)
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
    RENAME LICENSE.i2pd-tools COMPONENT Tools)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  # CI creates a signed public RouterInfo through the production serializer;
  # no binary fixture or private keys are installed or checked into the tree.
  add_executable(routerinfo-fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/routerinfo-fixture.cpp")
  target_compile_features(routerinfo-fixture PRIVATE cxx_std_23)
  target_include_directories(routerinfo-fixture PRIVATE "${LIBI2PD_DIR}")
  target_link_libraries(routerinfo-fixture PRIVATE
    libi2pd i2pd_warnings Threads::Threads)
  i2pd_enable_sanitizers(routerinfo-fixture)

  find_package(Perl)
  find_program(PROVE_EXECUTABLE prove)

  if(NOT PERL_EXECUTABLE)
    if(I2PD_REQUIRE_PERL)
      message(FATAL_ERROR "I2PD_REQUIRE_PERL=ON but no Perl interpreter was found")
    endif()
    message(STATUS "Perl not found: i2pdctl and Perl maintenance checks are not registered")
  endif()
  if(PERL_EXECUTABLE)
    add_test(NAME perl-i2pdctl-syntax
      COMMAND "${PERL_EXECUTABLE}" -c "${CMAKE_CURRENT_SOURCE_DIR}/scripts/i2pdctl")
    set_tests_properties(perl-i2pdctl-syntax PROPERTIES LABELS "perl;static" TIMEOUT 30)

    # Compile every other maintained Perl program/test without executing it.
    # This catches stale module names and accidental non-Perl syntax before
    # the regression tests run.
    set(I2PD_PERL_SYNTAX_SCRIPTS
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-links.pl"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-nav.pl"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-branding.pl"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-workflows.pl"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate-branding.t"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl-portability.t"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/maintenance-checkers.t"
      "${PROJECT_ROOT}/tests/interop/check-vectors.pl"
      "${PROJECT_ROOT}/android/tools/check-android-locales.pl"
      "${PROJECT_ROOT}/android/tools/check-android-jni-symbols.pl"
      "${PROJECT_ROOT}/i18n/check-i18n-catalogs.pl")
    foreach(script IN LISTS I2PD_PERL_SYNTAX_SCRIPTS)
      get_filename_component(script_name "${script}" NAME)
      string(REPLACE "." "-" script_name "${script_name}")
      add_test(NAME "perl-syntax-${script_name}"
        COMMAND "${PERL_EXECUTABLE}" -c "${script}")
      set_tests_properties("perl-syntax-${script_name}" PROPERTIES LABELS "perl;static" TIMEOUT 30)
    endforeach()

    add_test(NAME perl-portability
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl-portability.t")
    set_tests_properties(perl-portability PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;static"
      TIMEOUT 60)

    add_test(NAME perl-validate-branding
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate-branding.t")
    set_tests_properties(perl-validate-branding PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;static"
      TIMEOUT 60)

    # Negative fixtures for the maintenance checkers: a checker that silently
    # stops detecting a broken input must fail this suite.
    add_test(NAME perl-maintenance-checkers
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/maintenance-checkers.t")
    set_tests_properties(perl-maintenance-checkers PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;static"
      TIMEOUT 120)

    if(NOT IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
      add_test(NAME perl-install-layout
        COMMAND "${CMAKE_COMMAND}"
          "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
          "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/perl-install"
          "-DPERL_EXECUTABLE=${PERL_EXECUTABLE}"
          "-DINSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}"
          "-DINSTALL_DOCDIR=${CMAKE_INSTALL_DOCDIR}"
          "-DINSTALL_MANDIR=${CMAKE_INSTALL_MANDIR}"
          "-DTOOLS=${I2PD_TOOLS_INVENTORY}"
          "-DEXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
          "-DINSTALL_CONFIG=$<CONFIG>"
          "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/test-perl-install.cmake")
      set_tests_properties(perl-install-layout PROPERTIES
        LABELS "perl;install"
        TIMEOUT 120)
    endif()
  endif()
  if(PERL_EXECUTABLE AND PROVE_EXECUTABLE)
    add_test(NAME perl-i2pdctl
      COMMAND "${PROVE_EXECUTABLE}" -v "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t")
    set_tests_properties(perl-i2pdctl PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;integration"
      TIMEOUT 120)
  elseif(PERL_EXECUTABLE)
    # OpenBSD base Perl may not ship the prove wrapper. Running the TAP file
    # directly still exercises the same Test::More assertions.
    add_test(NAME perl-i2pdctl
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t")
    set_tests_properties(perl-i2pdctl PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;integration"
      TIMEOUT 120)
  endif()

  # Perl::Critic is deliberately opt-in: it is not part of the OpenBSD base
  # Perl runtime and no CPAN or runtime download is permitted.
  if(I2PD_PERL_CRITIC)
    if(NOT PERL_EXECUTABLE)
      message(FATAL_ERROR "I2PD_PERL_CRITIC=ON but Perl is unavailable")
    else()
      find_program(PERLCRITIC_EXECUTABLE perlcritic)
    endif()
    if(PERL_EXECUTABLE AND NOT PERLCRITIC_EXECUTABLE)
      message(FATAL_ERROR "I2PD_PERL_CRITIC=ON but perlcritic is unavailable")
    elseif(PERL_EXECUTABLE)
    add_test(NAME perl-critic
      COMMAND "${PERLCRITIC_EXECUTABLE}"
        --profile "${PROJECT_ROOT}/.perlcriticrc"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/i2pdctl"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-links.pl"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-nav.pl"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-branding.pl"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate-branding.t"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl-portability.t"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/maintenance-checkers.t"
        "${PROJECT_ROOT}/tests/interop/check-vectors.pl"
        "${PROJECT_ROOT}/android/tools/check-android-locales.pl"
        "${PROJECT_ROOT}/android/tools/check-android-jni-symbols.pl"
        "${PROJECT_ROOT}/i18n/check-i18n-catalogs.pl")
    set_tests_properties(perl-critic PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;static;critic"
      TIMEOUT 300)
    endif()
  endif()

  # Exercise the MSVC getopt shim on the host as well. Built with the test
  # macro so the implementation is compiled on POSIX too.
  add_executable(test-tools-getopt
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test-tools-getopt.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/common/getopt.cpp")
  target_compile_features(test-tools-getopt PRIVATE cxx_std_23)
  target_compile_definitions(test-tools-getopt PRIVATE I2PD_TOOLS_GETOPT_TEST)
  target_include_directories(test-tools-getopt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(test-tools-getopt PRIVATE /UNDEBUG)
  else()
    target_compile_options(test-tools-getopt PRIVATE -UNDEBUG)
  endif()
  i2pd_enable_sanitizers(test-tools-getopt)
  add_test(NAME tools-getopt COMMAND test-tools-getopt)
  set_tests_properties(tools-getopt PROPERTIES LABELS "tools;cli;unit" TIMEOUT 30)

  # Exact exit-code and stdout/stderr contracts, plus same-file and CRLF
  # regressions that the merged-stream smoke/regression scripts cannot see.
  add_test(NAME tools-cli
    COMMAND ${CMAKE_COMMAND}
      "-DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "-DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools-cli.cmake")
  set_tests_properties(tools-cli PROPERTIES TIMEOUT 120 LABELS "tools;cli;integration")

  add_test(NAME tools-smoke
    COMMAND ${CMAKE_COMMAND}
      -DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>
      "-DROUTERINFO_FIXTURE_HELPER=$<TARGET_FILE:routerinfo-fixture>"
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools.cmake")
  set_tests_properties(tools-smoke PROPERTIES TIMEOUT 60 LABELS "tools;cli;integration")

  add_test(NAME tools-regressions
    COMMAND ${CMAKE_COMMAND}
      "-DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "-DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools-regressions.cmake")
  set_tests_properties(tools-regressions PROPERTIES TIMEOUT 600 LABELS "tools;cli;integration")

  add_test(NAME tools-inventory
    COMMAND ${CMAKE_COMMAND}
      "-DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>"
      "-DTOOLS=${I2PD_TOOLS_INVENTORY}"
      "-DTOOLS_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      "-DTOOLS_UPSTREAM_MANIFEST=${PROJECT_ROOT}/contrib/upstream/tools-files.txt"
      "-DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools-inventory.cmake")
  set_tests_properties(tools-inventory PROPERTIES TIMEOUT 30 LABELS "tools;static")
endif()
