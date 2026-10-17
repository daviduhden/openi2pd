enable_testing()

function(i2pd_add_test name)
  add_executable(${name} "${name}.cpp")
  target_compile_features(${name} PRIVATE cxx_std_23)
  target_include_directories(${name} PRIVATE "${PROJECT_ROOT}/tools")
  target_link_libraries(${name} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
  if(MSVC)
    target_compile_options(${name} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${name} PRIVATE -UNDEBUG)
  endif()
  i2pd_enable_sanitizers(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

set(I2PD_TESTS
  test-http-merge_chunked
  test-http-req
  test-http-res
  test-http-url
  test-http-url_decode
  test-base-64
  test-gost
  test-gost-sig
  test-aeadchacha20poly1305
  test-blinding
  test-elligator
  test-eddsa
  test-aes
  test-tui-protocol
  test-routerinfo-firewall
)

foreach(test IN LISTS I2PD_TESTS)
  i2pd_add_test(${test})
endforeach()

# Shell-level checks that only need the built router binary.
if(TARGET i2pd)
  add_test(NAME i2pd-version COMMAND i2pd --version)
endif()

# Optional libFuzzer targets. They are not built by default and are not
# registered with CTest: run them explicitly with a corpus directory.
option(WITH_FUZZERS "Build libFuzzer targets (Clang)" OFF)
if(WITH_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "WITH_FUZZERS requires Clang")
  endif()
  foreach(fuzzer fuzz-tui-protocol fuzz-http-parser)
    add_executable(${fuzzer} "fuzz/${fuzzer}.cpp")
    target_compile_features(${fuzzer} PRIVATE cxx_std_23)
    target_link_libraries(${fuzzer} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
    target_compile_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()
