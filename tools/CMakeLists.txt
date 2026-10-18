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
  add_executable(${tool} "${CMAKE_CURRENT_SOURCE_DIR}/${tool}.cpp")
  target_compile_features(${tool} PRIVATE cxx_std_23)
  target_include_directories(${tool} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${LIBI2PD_DIR}"
    "${LIBI2PD_CLIENT_DIR}"
  )
  target_link_libraries(${tool} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings Threads::Threads)
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
  add_test(NAME tools-smoke
    COMMAND ${CMAKE_COMMAND}
      -DTOOLS_DIR=$<TARGET_FILE_DIR:keygen>
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DCMAKE_EXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-tools.cmake")
  set_tests_properties(tools-smoke PROPERTIES TIMEOUT 60)
endif()
