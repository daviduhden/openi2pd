# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# Auxiliary tools imported from PurpleI2P/i2pd-tools.
#
# They are built as independent executables against the in-tree libraries.
# There is no Git submodule, no external i2pd checkout and no separate Makefile.

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

foreach(tool IN LISTS I2PD_TOOLS)
  add_executable(${tool}
    "${CMAKE_CURRENT_SOURCE_DIR}/${tool}.cpp"
    # Portable getopt/getopt_long compatibility for MSVC; empty on POSIX.
    "${CMAKE_CURRENT_SOURCE_DIR}/common/getopt.cpp")
  target_compile_features(${tool} PRIVATE cxx_std_23)
  target_include_directories(${tool} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LIBI2PD_DIR}"
    "${LIBI2PD_CLIENT_DIR}"
  )
  target_link_libraries(${tool} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings Threads::Threads)
  if(WIN32)
    # Static consumers of libi2pd still need the WSA extension import library
    # for AcceptEx/GetAcceptExSockaddrs on MinGW. CMake does not always carry
    # the static library's PUBLIC list to an executable on MSYS2/Clang64.
    target_link_libraries(${tool} PRIVATE mswsock ws2_32)
  endif()
  i2pd_enable_sanitizers(${tool})
  install(TARGETS ${tool} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tools)
endforeach()

# i2pdctl is a Perl client for the retained I2PControl service. It uses only
# core Perl modules plus the system openssl binary for the self-signed HTTPS
# fallback; it is not a dependency of the daemon. Runtime requirements are a
# Perl 5 interpreter and either IO::Socket::SSL or the openssl command.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/i2pdctl"
  DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Tools)

if(BUILD_TESTING)
  find_package(Perl)
  find_program(PROVE_EXECUTABLE prove)

  if(PERL_EXECUTABLE)
    add_test(NAME perl-i2pdctl-syntax
      COMMAND "${PERL_EXECUTABLE}" -c "${CMAKE_CURRENT_SOURCE_DIR}/scripts/i2pdctl")
    set_tests_properties(perl-i2pdctl-syntax PROPERTIES LABELS "perl;static")

    # Compile every other maintained Perl program/test without executing it.
    # This catches stale module names and accidental non-Perl syntax before
    # the regression tests run.
    set(I2PD_PERL_SYNTAX_SCRIPTS
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-links.pl"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-branding.pl"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate-branding.t"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl-portability.t"
      "${PROJECT_ROOT}/tests/interop/check-vectors.pl"
      "${PROJECT_ROOT}/android/tools/check-english-only.pl")
    foreach(script IN LISTS I2PD_PERL_SYNTAX_SCRIPTS)
      get_filename_component(script_name "${script}" NAME)
      string(REPLACE "." "-" script_name "${script_name}")
      add_test(NAME "perl-syntax-${script_name}"
        COMMAND "${PERL_EXECUTABLE}" -c "${script}")
      set_tests_properties("perl-syntax-${script_name}" PROPERTIES LABELS "perl;static")
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
      LABELS "perl;static")

    if(NOT IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
      add_test(NAME perl-install-layout
        COMMAND "${CMAKE_COMMAND}"
          "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
          "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/perl-install"
          "-DPERL_EXECUTABLE=${PERL_EXECUTABLE}"
          "-DINSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}"
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
      LABELS "perl;integration")
  elseif(PERL_EXECUTABLE)
    # OpenBSD base Perl may not ship the prove wrapper. Running the TAP file
    # directly still exercises the same Test::More assertions.
    add_test(NAME perl-i2pdctl
      COMMAND "${PERL_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t")
    set_tests_properties(perl-i2pdctl PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;integration")
  endif()

  # Perl::Critic is deliberately opt-in: it is not part of the OpenBSD base
  # Perl runtime and no CPAN or runtime download is permitted.
  if(I2PD_PERL_CRITIC)
    if(NOT PERL_EXECUTABLE)
      message(FATAL_ERROR "I2PD_PERL_CRITIC requires Perl")
    endif()
    find_program(PERLCRITIC_EXECUTABLE perlcritic)
    if(NOT PERLCRITIC_EXECUTABLE)
      message(FATAL_ERROR "I2PD_PERL_CRITIC=ON requires the perlcritic executable")
    endif()
    add_test(NAME perl-critic
      COMMAND "${PERLCRITIC_EXECUTABLE}"
        --profile "${PROJECT_ROOT}/.perlcriticrc"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/i2pdctl"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-doc-links.pl"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-branding.pl"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/i2pdctl.t"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate-branding.t"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/perl-portability.t"
        "${PROJECT_ROOT}/tests/interop/check-vectors.pl"
        "${PROJECT_ROOT}/android/tools/check-english-only.pl")
    set_tests_properties(perl-critic PROPERTIES
      WORKING_DIRECTORY "${PROJECT_ROOT}"
      LABELS "perl;static;critic"
      TIMEOUT 300)
  endif()

  add_test(NAME tools-smoke
    COMMAND ${CMAKE_COMMAND}
      -DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools.cmake")
  set_tests_properties(tools-smoke PROPERTIES TIMEOUT 60 LABELS "tools;integration")

  add_test(NAME tools-regressions
    COMMAND ${CMAKE_COMMAND}
      "-DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "-DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools-regressions.cmake")
  set_tests_properties(tools-regressions PROPERTIES TIMEOUT 600 LABELS "tools;integration")

  string(REPLACE ";" "," I2PD_TOOLS_INVENTORY "${I2PD_TOOLS}")
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
