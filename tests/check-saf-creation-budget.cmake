# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: compile-free SAF creation input-retention contracts.

if(NOT PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/TorrentSafCreator.java" creator)
file(READ "${PROJECT_ROOT}/android/app/src/main/java/org/openi2pd/router/TorrentSafCreationBudget.java" budget)
file(READ "${PROJECT_ROOT}/android/app/src/main/cpp/i2pd_android.cpp" jni)
string(FIND "${creator}" "budget.document(child, path);" charge)
string(FIND "${creator}" "names.add(name)" retain)
if(charge LESS 0 OR retain LESS_EQUAL charge)
  message(FATAL_ERROR "SAF child text must be charged before collection retention")
endif()
string(FIND "${creator}" "budget.authority(document.toString());" authority)
# Allow formatter whitespace only at the constructor's call boundaries.
# Do not merge Java tokens or normalize quoted document operands.
function(saf_source_retention_position text output)
  string(REGEX MATCH
    "files[.]add[(][ \t\r\n]*new[ \t\r\n]+Source[(]document,[ \t\r\n]*path,"
    source_prefix "${text}")
  set(position -1)
  if(source_prefix)
    string(FIND "${text}" "${source_prefix}" position)
  endif()
  set(${output} "${position}" PARENT_SCOPE)
endfunction()
saf_source_retention_position("${creator}" source)
# Negative controls keep root/document identity and token separation.
saf_source_retention_position("files.add(new Source(root, path, 0));" wrong_document)
saf_source_retention_position("files.add(newSource(document, path, 0));" merged_token)
if(NOT wrong_document EQUAL -1 OR NOT merged_token EQUAL -1)
  message(FATAL_ERROR "SAF source boundary must preserve Java operand identities")
endif()
if(authority LESS 0 OR source LESS_EQUAL authority)
  message(FATAL_ERROR "SAF encoded URI must be charged before source retention")
endif()
string(REGEX MATCHALL "TorrentSafCreationBudget[.]hashAppend[(]hashes.size[(][)][)]" hashes "${creator}")
list(LENGTH hashes hash_checks)
if(NOT hash_checks EQUAL 2)
  message(FATAL_ERROR "Full and final partial hashes require the same pre-append bound")
endif()
foreach(required IN ITEMS "MAX_TEXT_BYTES = 8 * 1024 * 1024"
    "MAX_ID_BYTES = 8192" "MAX_PATH_BYTES = 8192"
    "MAX_HASH_BYTES = 5 * 1024 * 1024"
    "utf8Length(name) > 255" "Character.isHighSurrogate"
    "Character.isLowSurrogate" "MAX_TEXT_BYTES - documentIdBytes"
    "MAX_TEXT_BYTES - pathBytes" "MAX_HASH_BYTES - HASH_BYTES")
  string(FIND "${budget}" "${required}" bound)
  if(bound LESS 0)
    message(FATAL_ERROR "SAF creation UTF-8 budget missing: ${required}")
  endif()
endforeach()
string(FIND "${jni}" "MAX_METADATA_PATH_BYTES - pathBytes" aggregate)
if(aggregate LESS 0 OR jni MATCHES "bytes \\(path.get \\(\\), 8192\\)")
  message(FATAL_ERROR "JNI paths must use aggregate remaining capacity before copying")
endif()
