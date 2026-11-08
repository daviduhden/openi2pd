# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Preserve assertion evaluation in test and fuzzer translation units.

include_guard(GLOBAL)

function(i2pd_enable_test_assertions target)
  if(MSVC)
    # /UNDEBUG conflicts with Release's /DNDEBUG and produces D9025.
    # Force the test-only header before all source includes instead.
    target_compile_options(${target} PRIVATE
      "/FI${PROJECT_ROOT}/tests/TestAssertions.h")
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()
