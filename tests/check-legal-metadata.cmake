# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check legal metadata.
#
# Compile-free consistency check for LICENSE, NOTICE, documentation licences,
# package metadata and the Windows version resource. It never reads git history
# or runs a packaging tool.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing legal metadata file: ${path}")
  endif()
  file(READ "${path}" contents)
  set(${out_var} "${contents}" PARENT_SCOPE)
endfunction()

function(assert_present text needle description)
  string(FIND "${text}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${description}: missing '${needle}'")
  endif()
endfunction()

read_required("${PROJECT_ROOT}/LICENSE" license)
read_required("${PROJECT_ROOT}/android/LICENSE" android_license)
read_required("${PROJECT_ROOT}/docs/LICENSE.i2pd-docs-en" docs_license)
read_required("${PROJECT_ROOT}/tools/LICENSE" tools_license)
read_required("${PROJECT_ROOT}/NOTICE" notice)
read_required("${PROJECT_ROOT}/debian/copyright" debian_copyright)
read_required("${PROJECT_ROOT}/Win32/Resource.rc2" win_resource)
read_required("${PROJECT_ROOT}/docs/about/license.md" docs_about_license)
read_required("${PROJECT_ROOT}/docs/file-headers.md" docs_file_headers)
read_required("${PROJECT_ROOT}/build/CMakeLists.txt" build_cmake)
read_required("${PROJECT_ROOT}/debian/i2pd.install" debian_install)
read_required("${PROJECT_ROOT}/contrib/rpm/i2pd.spec" rpm_spec)
read_required("${PROJECT_ROOT}/contrib/rpm/i2pd-git.spec" rpm_git_spec)
read_required("${PROJECT_ROOT}/contrib/static/debian/i2pd.docs" static_docs)
read_required("${PROJECT_ROOT}/docs/CC-BY-SA-4.0" cc_notice)
read_required("${PROJECT_ROOT}/contrib/rpm/i2pd-git.spec" rpm_git_spec_license)

# Project licensors must agree across the code, Android copy and package
# metadata.
foreach(label IN ITEMS root android)
  if(label STREQUAL "root")
    set(file_text "${license}")
  else()
    set(file_text "${android_license}")
  endif()
  assert_present("${file_text}" "The PurpleI2P Project" "${label} licence upstream holder")
  assert_present("${file_text}" "David Uhden Collado" "${label} licence fork holder")
  assert_present("${file_text}" "Redistribution and use" "${label} licence family")
endforeach()
string(FIND "${license}" "SPDX-License-Identifier: BSD-3-Clause" root_spdx_position)
if(NOT root_spdx_position EQUAL -1)
  message(FATAL_ERROR "root licence must remain plain text without an SPDX line")
endif()
assert_present("${android_license}" "SPDX-License-Identifier: BSD-3-Clause" "Android SPDX")
assert_present("${tools_license}" "SPDX-License-Identifier: BSD-3-Clause" "tools SPDX")
assert_present("${cc_notice}" "Attribution-ShareAlike 4.0" "installed CC-BY-SA notice")
assert_present("${build_cmake}" "RENAME LICENSE.i2pd-tools" "CMake tools licence rename")
assert_present("${build_cmake}" "docs/CC-BY-SA-4.0" "CMake CC-BY-SA install")
assert_present("${docs_license}" "2017-2026, The PurpleI2P Project" "documentation upstream holder")
assert_present("${debian_copyright}" "The PurpleI2P Project" "Debian upstream holder")
assert_present("${debian_copyright}" "David Uhden Collado" "Debian fork holder")
assert_present("${debian_copyright}" "CC-BY-SA-4.0" "Debian CC-BY-SA stanza")

# The Windows resource is a legal notice, not a build detail.
assert_present("${win_resource}" "The PurpleI2P Project" "Windows LegalCopyright upstream holder")
assert_present("${win_resource}" "David Uhden Collado" "Windows LegalCopyright fork holder")

# The notice inventory and the two CC-BY-SA documents must stay explicit.
foreach(needle IN ITEMS "BSD 3-Clause" "CC-BY-SA-4.0" "Apache License 2.0"
    "docs/file-headers.md" "docs/torrent/snark-compatibility.md"
    "tools/LICENSE" "android/LICENSE")
  assert_present("${notice}" "${needle}" "NOTICE inventory")
endforeach()
assert_present("${docs_about_license}" "NOTICE" "documentation licence page NOTICE link")
assert_present("${docs_about_license}" "CC-BY-SA-4.0" "documentation licence page CC-BY-SA scope")
assert_present("${docs_file_headers}" "CC-BY-SA-4.0" "file-header policy CC-BY-SA scope")

# Every install path must ship the notice, not only the Debian manifest.
assert_present("${build_cmake}" "NOTICE" "CMake notice install")
assert_present("${debian_install}" "NOTICE" "Debian notice install")
assert_present("${rpm_spec}" "%doc LICENSE NOTICE" "RPM notice")
assert_present("${rpm_git_spec}" "%doc LICENSE NOTICE" "RPM git notice")
assert_present("${static_docs}" "src/NOTICE" "static package notice")
assert_present("${static_docs}" "src/LICENSE" "static package licence")
assert_present("${static_docs}" "src/docs/CC-BY-SA-4.0" "static package CC-BY-SA")
assert_present("${rpm_git_spec_license}" "LICENSE.i2pd-tools" "RPM tools licence")
assert_present("${rpm_git_spec_license}" "CC-BY-SA-4.0" "RPM CC-BY-SA notice")

foreach(document IN ITEMS "docs/file-headers.md" "docs/torrent/snark-compatibility.md")
  if(NOT EXISTS "${PROJECT_ROOT}/${document}")
    message(FATAL_ERROR "declared CC-BY-SA document is missing: ${document}")
  endif()
endforeach()

message(STATUS "legal-metadata-static: licence, notice and package metadata agree")
