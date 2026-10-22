# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check i18n.
#
# Compile-free check of native catalog registration, process-local language
# selection, TUI linkage and Android locale metadata. It never invokes a
# compiler, Gradle, CTest or a project binary.
#

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

function(read_required path out_var)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing i18n file: ${path}")
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
    message(FATAL_ERROR "${description}: found '${needle}'")
  endif()
endfunction()

set(top "${PROJECT_ROOT}/build/CMakeLists.txt")
set(i18n_cpp "${PROJECT_ROOT}/i18n/I18N.cpp")
set(langs "${PROJECT_ROOT}/i18n/I18N_langs.h")
set(spanish "${PROJECT_ROOT}/i18n/Spanish.cpp")
set(russian "${PROJECT_ROOT}/i18n/Russian.cpp")
set(chinese "${PROJECT_ROOT}/i18n/ChineseSimplified.cpp")
set(tui "${PROJECT_ROOT}/tui/main.cpp")
set(tui_client "${PROJECT_ROOT}/tui/TuiClient.cpp")
set(android_gradle "${PROJECT_ROOT}/android/app/build.gradle")
set(locales "${PROJECT_ROOT}/android/app/src/main/res/xml/locales_config.xml")
set(android_guard "${PROJECT_ROOT}/android/tools/check-android-locales.pl")

foreach(path IN ITEMS "${top}" "${i18n_cpp}" "${langs}" "${spanish}" "${russian}"
    "${chinese}" "${tui}" "${tui_client}" "${android_gradle}" "${locales}"
    "${android_guard}")
  read_required("${path}" ignored)
endforeach()

read_required("${top}" cmake_text)
read_required("${i18n_cpp}" i18n_text)
read_required("${langs}" langs_text)
read_required("${spanish}" spanish_text)
read_required("${russian}" russian_text)
read_required("${chinese}" chinese_text)
read_required("${tui}" tui_text)
read_required("${tui_client}" tui_client_text)
read_required("${android_gradle}" gradle_text)
read_required("${locales}" locales_text)
read_required("${android_guard}" guard_text)

file(GLOB native_catalog_sources "${PROJECT_ROOT}/i18n/*.cpp")
foreach(catalog IN LISTS native_catalog_sources)
  get_filename_component(catalog_name "${catalog}" NAME)
  if(catalog_name STREQUAL "I18N.cpp")
    continue()
  endif()
  string(FIND "${cmake_text}" "${catalog_name}" catalog_registered)
  if(catalog_registered EQUAL -1)
    message(FATAL_ERROR "LANG_SOURCES does not register ${catalog_name}")
  endif()
endforeach()

assert_present("${langs_text}" "namespace spanish" "Spanish locale accessor")
assert_present("${langs_text}" "namespace russian" "Russian locale accessor")
assert_present("${langs_text}" "namespace chinese_simplified" "Chinese locale accessor")
assert_present("${i18n_text}" "EnvironmentLanguage" "locale environment fallback")
assert_present("${i18n_text}" "LC_NUMERIC" "numeric locale pinning")
assert_absent("${i18n_text}" "ClientContext.h" "I18N must not depend on the router client")
assert_present("${cmake_text}" "Threads::Threads libi2pdlang" "TUI catalog linkage")
assert_present("${tui_text}" "SetLanguage" "TUI language selection")
assert_present("${tui_text}" "i2p::i18n::tr" "TUI translation boundary")
assert_present("${tui_client_text}" "--lang" "TUI language option")
assert_present("${gradle_text}" "localeFilters += [\"en\", \"es\", \"ru\", \"zh\"]"
  "Android locale filters")
assert_present("${locales_text}" "zh-CN" "Android Simplified Chinese locale")
assert_present("${guard_text}" "placeholder" "Android placeholder parity guard")
assert_absent("${guard_text}" "English-only" "legacy English-only guard")
assert_present("${guard_text}" "values-zh-rCN" "Simplified Chinese catalog check")

message(STATUS "i18n-static: OK")
