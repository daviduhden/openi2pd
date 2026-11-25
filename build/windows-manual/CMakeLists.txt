# See the LICENSE file at the top of the project tree for copyright and
# license details.

cmake_minimum_required(VERSION 3.24)
include(../cmake_modules/Version.cmake)
set(version_header "${CMAKE_CURRENT_SOURCE_DIR}/../../libi2pd/version.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${version_header}")
set_version("${version_header}" manual_version)
project(OpenI2PdWindowsManual VERSION "${manual_version}" LANGUAGES NONE)

include(../cmake/WindowsManual.cmake)
i2pd_add_windows_manual("${PROJECT_VERSION}" windows_manual_chm)
install(FILES "${windows_manual_chm}" DESTINATION share/doc/i2pd
  COMPONENT Documentation)
