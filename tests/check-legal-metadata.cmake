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

function(assert_absent text needle description)
  string(FIND "${text}" "${needle}" position)
  if(NOT position EQUAL -1)
    message(FATAL_ERROR "${description}: unexpected '${needle}'")
  endif()
endfunction()

read_required("${PROJECT_ROOT}/LICENSE" license)
read_required("${PROJECT_ROOT}/android/LICENSE" android_license)
read_required("${PROJECT_ROOT}/docs/LICENSE" docs_license)
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
read_required("${PROJECT_ROOT}/contrib/rpm/i2pd-git.spec" rpm_git_spec_license)
read_required("${PROJECT_ROOT}/contrib/static/i2pd.spec" static_spec)
read_required("${PROJECT_ROOT}/contrib/docker/Dockerfile" dockerfile)

file(GLOB_RECURSE repository_files LIST_DIRECTORIES FALSE "${PROJECT_ROOT}/*")
foreach(repository_file IN LISTS repository_files)
  if(repository_file MATCHES "/\\.git/")
    continue()
  endif()
  get_filename_component(repository_filename "${repository_file}" NAME)
  if(repository_filename STREQUAL "CC-BY-SA-4.0"
      OR repository_filename STREQUAL "LICENSE.i2pd-docs-en")
    message(FATAL_ERROR
      "${repository_filename} must be absent from the repository")
  endif()
endforeach()

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
assert_present("${build_cmake}" "RENAME LICENSE.i2pd-tools" "CMake tools licence rename")
assert_present("${docs_license}" "2013-2026, The PurpleI2P Project" "documentation upstream holder")
assert_present("${debian_copyright}" "The PurpleI2P Project" "Debian upstream holder")
assert_present("${debian_copyright}" "David Uhden Collado" "Debian fork holder")
assert_absent("${debian_copyright}" "CC-BY-SA-4.0" "Debian metadata must not retain the replaced CC license")
foreach(package_metadata IN ITEMS "${rpm_spec}" "${rpm_git_spec}" "${static_spec}" "${dockerfile}")
  assert_present("${package_metadata}" "BSD-3-Clause" "package BSD license metadata")
  assert_absent("${package_metadata}" "CC-BY-SA-4.0" "package metadata must not retain the replaced CC license")
endforeach()

# The Windows resource is a legal notice, not a build detail.
assert_present("${win_resource}" "The PurpleI2P Project" "Windows LegalCopyright upstream holder")
assert_present("${win_resource}" "David Uhden Collado" "Windows LegalCopyright fork holder")

# The notice inventory and project license scope must stay explicit.
foreach(needle IN ITEMS "BSD-3-Clause" "tools/LICENSE" "android/LICENSE"
    "docs/LICENSE" "debian/copyright")
  assert_present("${notice}" "${needle}" "NOTICE inventory")
endforeach()
assert_present("${docs_about_license}" "NOTICE" "documentation licence page NOTICE link")
assert_absent("${notice}" "CC-BY-SA-4.0" "NOTICE must not list CC-licensed project documentation")
assert_absent("${docs_about_license}" "CC-BY-SA-4.0" "documentation licence page must not list the replaced CC license")
foreach(document IN ITEMS "docs/file-headers.md" "docs/torrent/snark-compatibility.md")
  read_required("${PROJECT_ROOT}/${document}" document_license_text)
  assert_present("${document_license_text}" "SPDX-License-Identifier: BSD-3-Clause"
    "${document} BSD SPDX")
  assert_absent("${document_license_text}" "CC-BY-SA-4.0"
    "${document} must not retain the replaced CC license")
endforeach()

# Every install path must ship the notice, not only the Debian manifest.
assert_present("${build_cmake}" "NOTICE" "CMake notice install")
assert_present("${debian_install}" "NOTICE" "Debian notice install")
assert_present("${rpm_spec}" "%doc LICENSE NOTICE" "RPM notice")
assert_present("${rpm_git_spec}" "%doc LICENSE NOTICE" "RPM git notice")
assert_present("${static_docs}" "src/NOTICE" "static package notice")
assert_present("${static_docs}" "src/LICENSE" "static package licence")
assert_present("${rpm_git_spec_license}" "LICENSE.i2pd-tools" "RPM tools licence")
assert_absent("${build_cmake}" "docs/CC-BY-SA-4.0" "CMake must not install a standalone CC notice")
assert_absent("${static_docs}" "CC-BY-SA-4.0" "static package must not ship a CC notice")
assert_absent("${rpm_git_spec_license}" "CC-BY-SA-4.0" "RPM metadata must not retain the replaced CC license")

foreach(document IN ITEMS "docs/file-headers.md" "docs/torrent/snark-compatibility.md")
  if(NOT EXISTS "${PROJECT_ROOT}/${document}")
    message(FATAL_ERROR "BSD documentation file is missing: ${document}")
  endif()
endforeach()

message(STATUS "legal-metadata-static: licence, notice and package metadata agree")
