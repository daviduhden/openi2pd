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
  "Prepare for CI"
  "Tracking"
)

foreach(skill_file IN LISTS skill_files)
  file(READ "${skill_file}" content)

  file(STRINGS "${skill_file}" frontmatter LIMIT_COUNT 4)
  list(LENGTH frontmatter frontmatter_lines)
  if(frontmatter_lines LESS 4)
    message(FATAL_ERROR "Incomplete frontmatter: ${skill_file}")
  endif()
  list(GET frontmatter 0 frontmatter_open)
  list(GET frontmatter 1 name_line)
  list(GET frontmatter 2 description_line)
  list(GET frontmatter 3 frontmatter_close)
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

  string(REGEX MATCHALL "references/[A-Za-z0-9_.-]+[.]md" reference_links "${content}")
  foreach(reference_link IN LISTS reference_links)
    if(NOT EXISTS "${skill_dir}/${reference_link}")
      message(FATAL_ERROR
        "Broken reference link '${reference_link}' in ${skill_file}")
    endif()
  endforeach()
endforeach()

message(STATUS "agent-skills-static: OK")
