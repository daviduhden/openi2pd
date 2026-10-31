# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check i18n.
#
# Compile-free check of native catalog registration, process-local
# language selection, TUI linkage and Android locale metadata. It never
# invokes a compiler, Gradle, CTest or a project binary.
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
set(language_policy "${PROJECT_ROOT}/docs/i18n.md")
set(readme "${PROJECT_ROOT}/README.md")
set(docs_index "${PROJECT_ROOT}/docs/index.md")
set(config_doc "${PROJECT_ROOT}/docs/user-guide/configuration.md")
set(sample_config "${PROJECT_ROOT}/contrib/i2pd.conf")

foreach(path IN ITEMS "${top}" "${i18n_cpp}" "${langs}" "${spanish}" "${russian}"
    "${chinese}" "${tui}" "${tui_client}" "${android_gradle}" "${locales}"
    "${android_guard}" "${language_policy}" "${readme}" "${docs_index}"
    "${config_doc}" "${sample_config}")
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
read_required("${language_policy}" language_policy_text)
read_required("${readme}" readme_text)
read_required("${docs_index}" docs_index_text)
read_required("${config_doc}" config_doc_text)
read_required("${sample_config}" sample_config_text)

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
assert_present("${cmake_text}" "Threads::Threads \${I2PD_EXTRA_LIBS} libi2pdlang" "TUI catalog linkage")
assert_present("${tui_text}" "SetLanguage" "TUI language selection")
assert_present("${tui_text}" "i2p::i18n::tr" "TUI translation boundary")
# tr() returns std::string_view; C++23 has no operator+(std::string,
# string_view). Every concatenated translation must be materialised as
# std::string first.
assert_absent("${tui_text}" "+ i2p::i18n::tr (" "TUI translation concatenation must materialise std::string")
assert_present("${tui_client_text}" "--lang" "TUI language option")
assert_present("${gradle_text}" "localeFilters += [\"en\", \"es\", \"ru\", \"zh\"]"
  "Android locale filters")
assert_present("${locales_text}" "zh-CN" "Android Simplified Chinese locale")
assert_present("${guard_text}" "placeholder" "Android placeholder parity guard")
assert_absent("${guard_text}" "English-only" "legacy English-only guard")
assert_present("${guard_text}" "values-zh-rCN" "Simplified Chinese catalog check")

# The four-language policy is documented once and linked from the entry
# pages.
foreach(tag IN ITEMS "en" "es" "ru" "zh-Hans" "zh-CN" "zh-TW" "zh-Hant")
  assert_present("${language_policy_text}" "${tag}" "language policy tag ${tag}")
endforeach()
assert_present("${readme_text}" "docs/i18n.md" "README language-policy link")
assert_present("${docs_index_text}" "i18n.md" "documentation index language-policy link")
assert_present("${config_doc_text}" "en, es, ru, zh-Hans" "configuration language table")
assert_present("${sample_config_text}" "docs/i18n.md" "sample configuration language reference")
assert_absent("${sample_config_text}" "built-in web/proxy" "removed web-console language wording")

# Localized confirmation hints must match the parser: the catalogs
# advertise [s/N] and [д/Н], so the TUI has to accept those letters as
# well.
assert_present("${spanish_text}" "[s/N]" "Spanish confirm hint")
assert_present("${russian_text}" "[д/Н]" "Russian confirm hint")
assert_present("${tui_text}" "IsAffirmative" "localized confirmation parser")
assert_present("${tui_text}" "answer == \"sí\"" "Spanish affirmative token")

# Status and prompt strings are translated at their boundary; rendering
# a prefixed status line through PutLine alone never matched a catalog
# key.
assert_present("${tui_text}" "void App::SetStatus (std::string message, bool error)"
  "SetStatus definition")
assert_present("${tui_text}" "i2p::i18n::tr (std::string_view (message))"
  "SetStatus catalog lookup")
assert_present("${tui_text}" "i2p::i18n::tr (std::string_view (label))"
  "BeginPrompt catalog lookup")

# Multibyte labels must use display columns, never byte length.
string(FIND "${tui_text}" "DisplayWidth (prefix)" detail_width)
if(detail_width EQUAL -1)
  message(FATAL_ERROR "BuildDetailGeneral sizes its wrap budget from byte length")
endif()
assert_present("${tui_text}" "TorrentStatusText" "localized torrent status rendering")

# Full catalog contract: key/placeholder/plural parity, no translated
# identifiers and every tr()/SetStatus()/BeginPrompt() literal present.
find_program(PERL_EXECUTABLE NAMES perl)
if(NOT PERL_EXECUTABLE)
  message(STATUS "Perl not found: native catalog contract check skipped")
else()
  execute_process(
    COMMAND "${PERL_EXECUTABLE}" "${PROJECT_ROOT}/i18n/check-i18n-catalogs.pl"
      --root "${PROJECT_ROOT}"
    RESULT_VARIABLE catalog_result
    OUTPUT_VARIABLE catalog_output
    ERROR_VARIABLE catalog_error)
  if(NOT catalog_result EQUAL 0)
    message(FATAL_ERROR "i18n catalog check failed:\n${catalog_output}${catalog_error}")
  endif()
endif()

message(STATUS "i18n-static: OK")
