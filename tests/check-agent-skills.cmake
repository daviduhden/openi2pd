# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check Agent Skills.
#
# Static CTest check of the .agents/skills contract. It reads the checked-in
# SKILL.md files and references only; it does not execute a skill, build a
# target, run a workflow or call an external validator.
#
# It verifies:
# - every expected openi2pd-* directory has a SKILL.md;
# - frontmatter starts and ends with ---;
# - name matches the directory name;
# - description starts with "Use when";
# - the required procedural sections are present;
# - every skill links to AGENTS.md;
# - every references/*.md link resolves; and
# - each skill has at least one reference file.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

# Script mode does not inherit the project's minimum version, so enable the
# in-list operator used by the README inventory check explicitly.
cmake_policy(SET CMP0057 NEW)

set(skills_root "${PROJECT_ROOT}/.agents/skills")
if(NOT IS_DIRECTORY "${skills_root}")
  message(FATAL_ERROR "Missing Agent Skills directory: ${skills_root}")
endif()

set(expected_skills
  openi2pd-android
  openi2pd-build
  openi2pd-ci
  openi2pd-cpp23
  openi2pd-cryptography
  openi2pd-documentation
  openi2pd-i18n
  openi2pd-i2p-interop
  openi2pd-linux-windows
  openi2pd-memory-security
  openi2pd-openbsd
  openi2pd-packaging
  openi2pd-privsep
  openi2pd-tooling
  openi2pd-torrent
  openi2pd-tui
  openi2pd-upstream
)

foreach(expected IN LISTS expected_skills)
  if(NOT EXISTS "${skills_root}/${expected}/SKILL.md")
    message(FATAL_ERROR "Missing Agent Skill: ${expected}/SKILL.md")
  endif()
endforeach()

file(GLOB entries "${skills_root}/*")
foreach(entry IN LISTS entries)
  if(IS_DIRECTORY "${entry}")
    get_filename_component(entry_name "${entry}" NAME)
    if(entry_name MATCHES "^openi2pd-" AND NOT EXISTS "${entry}/SKILL.md")
      message(FATAL_ERROR "Skill directory without SKILL.md: ${entry}")
    endif()
  endif()
endforeach()

file(GLOB skill_files "${skills_root}/*/SKILL.md")
if(NOT skill_files)
  message(FATAL_ERROR "No Agent Skill files found under ${skills_root}")
endif()

set(required_sections
  "Activate when"
  "Inspect first"
  "Reference sources"
  "Reconnaissance"
  "Parallel implementation"
  "Conventions"
  "Known issues and decisions"
  "Do not"
  "Prepare for CI"
  "Maintenance & Continuous Learning"
  "Tracking"
  "References"
)

set(readme_file "${skills_root}/README.md")
if(NOT EXISTS "${readme_file}")
  message(FATAL_ERROR "Missing Agent Skills discovery index: ${readme_file}")
endif()
file(READ "${readme_file}" readme_text)
file(STRINGS "${readme_file}" readme_header LIMIT_COUNT 3)
list(JOIN readme_header "\n" readme_header_text)
string(FIND "${readme_header_text}" "See the LICENSE file" readme_license)
if(readme_license EQUAL -1)
  message(FATAL_ERROR "Agent Skills README lacks the LICENSE header")
endif()
foreach(expected IN LISTS expected_skills)
  string(FIND "${readme_text}" "`${expected}`" readme_position)
  if(readme_position EQUAL -1)
    message(FATAL_ERROR "README inventory does not list ${expected}")
  endif()
endforeach()
# file(READ) keeps semicolons that would split a list; iterate real lines so
# every referenced skill is validated independently.
file(STRINGS "${readme_file}" readme_lines)
foreach(line IN LISTS readme_lines)
  if(line MATCHES "`(openi2pd-[a-z0-9-]+)`")
    set(_listed "${CMAKE_MATCH_1}")
    if(NOT _listed IN_LIST expected_skills)
      message(FATAL_ERROR "README lists unknown skill ${_listed}")
    endif()
  endif()
endforeach()

# The inventory is bidirectional: no unlisted openi2pd-* directory may hide.
file(GLOB skill_entries LIST_DIRECTORIES true "${skills_root}/*")
set(actual_skills)
foreach(entry IN LISTS skill_entries)
  if(IS_DIRECTORY "${entry}")
    get_filename_component(entry_name "${entry}" NAME)
    if(entry_name MATCHES "^openi2pd-")
      list(APPEND actual_skills "${entry_name}")
    else()
      message(FATAL_ERROR "Unexpected directory in .agents/skills: ${entry_name}")
    endif()
  endif()
endforeach()
list(SORT actual_skills)
set(sorted_expected ${expected_skills})
list(SORT sorted_expected)
if(NOT actual_skills STREQUAL sorted_expected)
  message(FATAL_ERROR
    "Agent Skill inventory drift. Expected ${sorted_expected}; found ${actual_skills}")
endif()

set(descriptions)
foreach(skill_file IN LISTS skill_files)
  file(READ "${skill_file}" content)
  string(REPLACE "\r" "" content "${content}")

  file(STRINGS "${skill_file}" frontmatter LIMIT_COUNT 4)
  list(LENGTH frontmatter frontmatter_lines)
  if(frontmatter_lines LESS 4)
    message(FATAL_ERROR "Incomplete frontmatter: ${skill_file}")
  endif()
  list(GET frontmatter 0 frontmatter_open)
  list(GET frontmatter 1 name_line)
  list(GET frontmatter 2 description_line)
  list(GET frontmatter 3 frontmatter_close)
  string(REPLACE "\r" "" frontmatter_open "${frontmatter_open}")
  string(REPLACE "\r" "" name_line "${name_line}")
  string(REPLACE "\r" "" description_line "${description_line}")
  string(REPLACE "\r" "" frontmatter_close "${frontmatter_close}")
  if(NOT frontmatter_open STREQUAL "---")
    message(FATAL_ERROR "Malformed opening frontmatter delimiter: ${skill_file}")
  endif()
  if(NOT frontmatter_close STREQUAL "---")
    message(FATAL_ERROR "Malformed closing frontmatter delimiter: ${skill_file}")
  endif()

  string(REGEX MATCH "^name: ([a-z0-9-]+)$" name_match "${name_line}")
  if(NOT name_match)
    message(FATAL_ERROR "Invalid or missing name: ${skill_file}")
  endif()
  set(declared_name "${CMAKE_MATCH_1}")

  get_filename_component(skill_dir "${skill_file}" DIRECTORY)
  get_filename_component(directory_name "${skill_dir}" NAME)
  if(NOT declared_name STREQUAL directory_name)
    message(FATAL_ERROR
      "Skill name '${declared_name}' does not match directory '${directory_name}': ${skill_file}")
  endif()

  string(REGEX MATCH "^description: Use when .+" description_match "${description_line}")
  if(NOT description_match)
    message(FATAL_ERROR "description must start with 'Use when': ${skill_file}")
  endif()
  string(REGEX REPLACE "^description: " "" description_text "${description_line}")
  if(description_text MATCHES ": | #")
    message(FATAL_ERROR "description must be a plain single-line YAML scalar: ${skill_file}")
  endif()
  string(LENGTH "${description_line}" description_length)
  if(description_length GREATER 300)
    message(FATAL_ERROR "description is longer than 300 characters: ${skill_file}")
  endif()
  list(FIND descriptions "${description_text}" duplicate_description)
  if(NOT duplicate_description EQUAL -1)
    message(FATAL_ERROR "duplicate skill description in ${skill_file}")
  endif()
  list(APPEND descriptions "${description_text}")

  string(FIND "${content}" "AGENTS.md" agents_position)
  if(agents_position EQUAL -1)
    message(FATAL_ERROR "Skill does not reference AGENTS.md: ${skill_file}")
  endif()

  foreach(section IN LISTS required_sections)
    string(FIND "${content}" "## ${section}" section_position)
    if(section_position EQUAL -1)
      message(FATAL_ERROR "Missing '## ${section}' in ${skill_file}")
    endif()
  endforeach()

  file(GLOB reference_files "${skill_dir}/references/*.md")
  if(NOT reference_files)
    message(FATAL_ERROR "Skill has no references/*.md: ${skill_file}")
  endif()
  foreach(reference_file IN LISTS reference_files)
    get_filename_component(reference_name "${reference_file}" NAME)
    string(FIND "${content}" "${reference_name}" reference_used)
    if(reference_used EQUAL -1)
      message(FATAL_ERROR "Unlinked reference ${reference_name} in ${skill_file}")
    endif()
    file(STRINGS "${reference_file}" reference_header LIMIT_COUNT 3)
    list(JOIN reference_header "\n" reference_header_text)
    string(FIND "${reference_header_text}" "See the LICENSE file" reference_license)
    if(reference_license EQUAL -1)
      message(FATAL_ERROR "Reference lacks the LICENSE header: ${reference_file}")
    endif()
  endforeach()

  string(REGEX MATCHALL "references/[A-Za-z0-9_.-]+[.]md" reference_links "${content}")
  foreach(reference_link IN LISTS reference_links)
    if(NOT EXISTS "${skill_dir}/${reference_link}")
      message(FATAL_ERROR
        "Broken reference link '${reference_link}' in ${skill_file}")
    endif()
  endforeach()
endforeach()

file(READ "${PROJECT_ROOT}/AGENTS.md" agents_text)
foreach(required_policy IN ITEMS
    "Never log, format, echo or retain passwords"
    "Raw `this` is forbidden"
    "a stopped"
    "never perform a blocking read on an event-loop thread"
    "Never chmod a pathname and reopen it for secret output"
    "Do not duplicate normative policy"
    "Source documentation policy"
    "Public APIs must state their contracts"
    "Explain security decisions"
    "Update affected comments whenever implementation or interfaces change"
    "Do not retain comments that contradict the code"
    "Every repository modification MUST include an agent/skills synchronization step."
    "Every repository change must include a meaningful, reusable update")
  string(FIND "${agents_text}" "${required_policy}" policy_position)
  if(policy_position EQUAL -1)
    message(FATAL_ERROR "AGENTS security policy drift: ${required_policy}")
  endif()
endforeach()

file(READ "${skills_root}/openi2pd-memory-security/SKILL.md" memory_security_skill)
foreach(required_skill_rule IN ITEMS
    "Treat logs and error strings as persistence boundaries"
    "A byte limit does not prevent a"
    "chmod followed by reopen is a symlink/TOCTOU defect")
  string(FIND "${memory_security_skill}" "${required_skill_rule}" rule_position)
  if(rule_position EQUAL -1)
    message(FATAL_ERROR "memory-security skill policy drift: ${required_skill_rule}")
  endif()
endforeach()

file(READ "${skills_root}/openi2pd-cpp23/SKILL.md" cpp23_skill)
foreach(required_skill_rule IN ITEMS
    "A cancellable operation is not a lifetime barrier"
    "strong callback cycles in long-lived registrations")
  string(FIND "${cpp23_skill}" "${required_skill_rule}" rule_position)
  if(rule_position EQUAL -1)
    message(FATAL_ERROR "C++23 skill policy drift: ${required_skill_rule}")
  endif()
endforeach()

file(READ "${skills_root}/openi2pd-linux-windows/SKILL.md" linux_windows_skill)
foreach(required_skill_rule IN ITEMS
    "Private files created by shared filesystem helpers require a protected DACL"
    "Do not treat POSIX mode bits or an inherited")
  string(FIND "${linux_windows_skill}" "${required_skill_rule}" rule_position)
  if(rule_position EQUAL -1)
    message(FATAL_ERROR "Linux/Windows skill policy drift: ${required_skill_rule}")
  endif()
endforeach()

message(STATUS "agent-skills-static: OK")
