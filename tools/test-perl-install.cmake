# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test perl install.
#
# CTest install-layout smoke test that installs the Tools component into
# a private prefix and checks i2pdctl's shebang, Perl syntax and
# executable mode. The private prefix is deleted and no system prefix is
# touched.
#

foreach(required BUILD_DIR WORK_DIR PERL_EXECUTABLE INSTALL_BINDIR INSTALL_DOCDIR INSTALL_MANDIR TOOLS)
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

# The shared tools licence belongs to Documentation when that component
# is enabled (the Tools rule is conditioned on the same option), so
# install the documentation component into the private prefix before
# checking attribution.
if(I2PD_INSTALL_DOCS)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
      --prefix "${WORK_DIR}" --component Documentation ${config_args}
    RESULT_VARIABLE docs_install_result
    OUTPUT_VARIABLE docs_install_output
    ERROR_VARIABLE docs_install_error)
  if(NOT docs_install_result EQUAL 0)
    message(FATAL_ERROR
      "Documentation install failed (${docs_install_result}): ${docs_install_output}${docs_install_error}")
  endif()
endif()

if(IS_ABSOLUTE "${INSTALL_BINDIR}")
  message(FATAL_ERROR
    "perl-install-layout requires a relative CMAKE_INSTALL_BINDIR, got ${INSTALL_BINDIR}")
endif()
set(installed "${WORK_DIR}/${INSTALL_BINDIR}/i2pdctl")

if(NOT EXISTS "${installed}")
  message(FATAL_ERROR "i2pdctl was not installed at ${installed}")
endif()

string(REPLACE "," ";" TOOL_NAMES "${TOOLS}")
foreach(tool IN LISTS TOOL_NAMES)
  set(tool_path "${WORK_DIR}/${INSTALL_BINDIR}/${tool}${EXE_SUFFIX}")
  if(NOT EXISTS "${tool_path}")
    message(FATAL_ERROR "native tool was not installed: ${tool_path}")
  endif()
  if(UNIX)
    execute_process(
      COMMAND "${PERL_EXECUTABLE}" -e "exit((-x $ARGV[0]) ? 0 : 1)" "${tool_path}"
      RESULT_VARIABLE executable_result)
    if(NOT executable_result EQUAL 0)
      message(FATAL_ERROR "installed tool is not executable: ${tool_path}")
    endif()
  endif()
endforeach()

set(installed_license "${WORK_DIR}/${INSTALL_DOCDIR}/LICENSE.i2pd-tools")
if(NOT EXISTS "${installed_license}")
  message(FATAL_ERROR "tools attribution was not installed: ${installed_license}")
endif()

set(installed_man "${WORK_DIR}/${INSTALL_MANDIR}/man1/i2pdctl.1")
if(NOT EXISTS "${installed_man}")
  message(FATAL_ERROR "i2pdctl man page was not installed in the Tools component: ${installed_man}")
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
