# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
#
# Install-layout smoke test for the Perl i2pdctl client. It installs only the
# Tools component into a private prefix, verifies the shebang, mode and syntax,
# and removes the private prefix afterwards. No system prefix is touched.

foreach(required BUILD_DIR WORK_DIR PERL_EXECUTABLE INSTALL_BINDIR)
  if(NOT ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
set(install_config "${INSTALL_CONFIG}")
if(install_config STREQUAL "")
  set(install_config "$ENV{CTEST_CONFIGURATION_TYPE}")
endif()
if(install_config STREQUAL "" AND DEFINED CMAKE_BUILD_TYPE)
  set(install_config "${CMAKE_BUILD_TYPE}")
endif()
set(config_args)
if(NOT install_config STREQUAL "")
  list(APPEND config_args --config "${install_config}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
    --prefix "${WORK_DIR}" --component Tools ${config_args}
  RESULT_VARIABLE install_result
  OUTPUT_VARIABLE install_output
  ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR
    "Tools install failed (${install_result}): ${install_output}${install_error}")
endif()

if(IS_ABSOLUTE "${INSTALL_BINDIR}")
  message(FATAL_ERROR
    "perl-install-layout requires a relative CMAKE_INSTALL_BINDIR, got ${INSTALL_BINDIR}")
endif()
set(installed "${WORK_DIR}/${INSTALL_BINDIR}/i2pdctl")

if(NOT EXISTS "${installed}")
  message(FATAL_ERROR "i2pdctl was not installed at ${installed}")
endif()

file(STRINGS "${installed}" shebang LIMIT_COUNT 1)
if(NOT shebang MATCHES "^#!/usr/bin/env perl")
  message(FATAL_ERROR "installed i2pdctl has an unexpected shebang: ${shebang}")
endif()

execute_process(
  COMMAND "${PERL_EXECUTABLE}" -c "${installed}"
  RESULT_VARIABLE syntax_result
  OUTPUT_VARIABLE syntax_output
  ERROR_VARIABLE syntax_error)
if(NOT syntax_result EQUAL 0)
  message(FATAL_ERROR
    "installed i2pdctl is not valid Perl: ${syntax_output}${syntax_error}")
endif()

if(UNIX)
  execute_process(
    COMMAND "${PERL_EXECUTABLE}" -e "exit((-x $ARGV[0]) ? 0 : 1)" "${installed}"
    RESULT_VARIABLE executable_result)
  if(NOT executable_result EQUAL 0)
    message(FATAL_ERROR "installed i2pdctl is not executable: ${installed}")
  endif()
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "perl-install-layout: installed and validated ${installed}")
