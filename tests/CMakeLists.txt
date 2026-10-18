# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
enable_testing()

function(i2pd_add_test name)
  add_executable(${name} "${name}.cpp")
  target_compile_features(${name} PRIVATE cxx_std_23)
  if(name STREQUAL "test-privsep-protocol")
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/privsep/Channel.cpp")
  elseif(name STREQUAL "test-privsep-log")
    target_sources(${name} PRIVATE
      "${PROJECT_ROOT}/privsep/Channel.cpp"
      "${PROJECT_ROOT}/privsep/LogWorker.cpp")
  elseif(name STREQUAL "test-tui-terminal")
    # Header-only shared key/attribute contract; no ncurses or console init.
    target_include_directories(${name} PRIVATE "${PROJECT_ROOT}/tui")
  elseif(name STREQUAL "test-tui-pipe-name")
    # The pipe-name helper is portable; keep the test runnable on Unix as well
    # as Windows.
    target_sources(${name} PRIVATE "${PROJECT_ROOT}/tui/TuiWin32Ipc.cpp")
    target_include_directories(${name} PRIVATE "${PROJECT_ROOT}/tui")
  elseif(name STREQUAL "test-tui-win32-pipe")
    # Exercises the Win32 named-pipe and DACL primitives used by the Windows
    # TUI transport. It does not validate a running console or kernel ACLs
    # across user accounts.
    target_link_libraries(${name} PRIVATE advapi32)
  endif()
  target_include_directories(${name} PRIVATE "${PROJECT_ROOT}" "${PROJECT_ROOT}/tools")
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
  test-tui-terminal
  test-tui-pipe-name
  test-routerinfo-firewall
  test-torrent-bencode
  test-protocol-bounds
)

if(UNIX)
  list(APPEND I2PD_TESTS test-privsep-protocol test-privsep-log)
endif()

if(WIN32)
  # Windows-only transport primitives. The test is compiled but no live
  # console, service account or cross-user ACL check is performed.
  list(APPEND I2PD_TESTS test-tui-win32-pipe)
endif()

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
  foreach(fuzzer fuzz-tui-protocol fuzz-http-parser fuzz-protocol-bounds)
    add_executable(${fuzzer} "fuzz/${fuzzer}.cpp")
    target_compile_features(${fuzzer} PRIVATE cxx_std_23)
    target_link_libraries(${fuzzer} PRIVATE libi2pd libi2pdclient libi2pdlang i2pd_warnings)
    target_compile_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()
