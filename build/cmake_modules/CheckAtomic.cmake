# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: CheckAtomic.
#
# Configures build and manual CI integration.
#

# atomic builtins are required for threading support.

include(CheckCXXSourceCompiles)
include(CheckLibraryExists)

# Report the extra library the host really needs. Consumers link this result
# instead of reading the probe-scope CMAKE_REQUIRED_LIBRARIES side effect.
set(I2PD_ATOMIC_LIBRARIES "")
set(_i2pd_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")

# Sometimes linking against libatomic is required for atomic ops, if
# the platform doesn't support lock-free atomics.

function(check_working_cxx_atomics varname)
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++23")
  CHECK_CXX_SOURCE_COMPILES("
#include <atomic>
std::atomic<int> x;
std::atomic<short> y;
std::atomic<char> z;
int main() {
  ++z;
  ++y;
  return ++x;
}
" ${varname})
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
endfunction(check_working_cxx_atomics)

function(check_working_cxx_atomics64 varname)
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  set(CMAKE_REQUIRED_FLAGS "-std=c++23 ${CMAKE_REQUIRED_FLAGS}")
  CHECK_CXX_SOURCE_COMPILES("
#include <atomic>
#include <cstdint>
std::atomic<uint64_t> x (0);
int main() {
  uint64_t i = x.load(std::memory_order_relaxed);
  (void)i;
  return 0;
}
" ${varname})
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
endfunction(check_working_cxx_atomics64)


# Check for (non-64-bit) atomic operations.
if(MSVC)
  set(HAVE_CXX_ATOMICS_WITHOUT_LIB True)
else()
  # First check if atomics work without the library.
  check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITHOUT_LIB)
  # If not, check if the library exists, and atomics work with it.
  if(NOT HAVE_CXX_ATOMICS_WITHOUT_LIB)
    check_library_exists(atomic __atomic_fetch_add_4 "" HAVE_LIBATOMIC)
    if(HAVE_LIBATOMIC)
      list(APPEND CMAKE_REQUIRED_LIBRARIES "atomic")
      set(I2PD_ATOMIC_LIBRARIES atomic)
      check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITH_LIB)
      if (NOT HAVE_CXX_ATOMICS_WITH_LIB)
        message(FATAL_ERROR "Host compiler must support std::atomic!")
      endif()
    else()
      message(FATAL_ERROR "Host compiler appears to require libatomic, but cannot find it.")
    endif()
  endif()
endif()

# Check for 64 bit atomic operations.
if(MSVC)
  set(HAVE_CXX_ATOMICS64_WITHOUT_LIB True)
else()
  # First check if atomics work without the library.
  check_working_cxx_atomics64(HAVE_CXX_ATOMICS64_WITHOUT_LIB)
  # If not, check if the library exists, and atomics work with it.
  if(NOT HAVE_CXX_ATOMICS64_WITHOUT_LIB)
    check_library_exists(atomic __atomic_load_8 "" HAVE_CXX_LIBATOMICS64)
    if(HAVE_CXX_LIBATOMICS64)
      list(APPEND CMAKE_REQUIRED_LIBRARIES "atomic")
      set(I2PD_ATOMIC_LIBRARIES atomic)
      check_working_cxx_atomics64(HAVE_CXX_ATOMICS64_WITH_LIB)
      if (NOT HAVE_CXX_ATOMICS64_WITH_LIB)
        message(FATAL_ERROR "Host compiler must support 64-bit std::atomic!")
      endif()
    else()
      message(FATAL_ERROR "Host compiler appears to require libatomic for 64-bit operations, but cannot find it.")
    endif()
  endif()
endif()

set(CMAKE_REQUIRED_LIBRARIES "${_i2pd_saved_required_libraries}")
unset(_i2pd_saved_required_libraries)
