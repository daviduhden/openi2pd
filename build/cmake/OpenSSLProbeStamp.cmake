# See the LICENSE file at the top of the project tree for copyright
# and license details.

# Cache identity for compile and link probes, independent of file access.
# Length-prefixed fields preserve list separators and delimiter characters.
function(i2pd_openssl_probe_stamp output)
  set(identity "openi2pd-openssl-probes-v2;")
  foreach(input IN ITEMS
      OPENSSL_INCLUDE_DIR OPENSSL_SSL_LIBRARY OPENSSL_CRYPTO_LIBRARY
      I2PD_OPENSSL_KIND I2PD_OPENSSL_VERSION OPENSSL_USE_STATIC_LIBS
      OPENSSL_MSVC_STATIC_RT CMAKE_CXX_COMPILER CMAKE_CXX_COMPILER_ID
      CMAKE_CXX_COMPILER_VERSION CMAKE_CXX_COMPILER_TARGET CMAKE_SYSROOT
      CMAKE_SYSROOT_COMPILE CMAKE_SYSROOT_LINK CMAKE_OSX_ARCHITECTURES
      CMAKE_OSX_DEPLOYMENT_TARGET CMAKE_MSVC_RUNTIME_LIBRARY
      CMAKE_CXX_STANDARD CMAKE_CXX_EXTENSIONS CMAKE_CXX_FLAGS
      CMAKE_EXE_LINKER_FLAGS CMAKE_REQUIRED_FLAGS CMAKE_REQUIRED_DEFINITIONS
      CMAKE_REQUIRED_INCLUDES CMAKE_REQUIRED_LIBRARIES
      CMAKE_REQUIRED_LINK_OPTIONS CMAKE_REQUIRED_LINK_DIRECTORIES
      CMAKE_TRY_COMPILE_CONFIGURATION CMAKE_BUILD_TYPE)
    string(LENGTH "${${input}}" size)
    string(APPEND identity "${input}:${size}:${${input}};")
  endforeach()
  # Multi-config generators can choose a different try_compile lane.
  set(configurations ${CMAKE_CONFIGURATION_TYPES} "${CMAKE_BUILD_TYPE}"
    "${CMAKE_TRY_COMPILE_CONFIGURATION}" DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  list(REMOVE_DUPLICATES configurations)
  foreach(configuration IN LISTS configurations)
    string(TOUPPER "${configuration}" configuration)
    foreach(prefix IN ITEMS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS)
      set(input "${prefix}_${configuration}")
      string(LENGTH "${${input}}" size)
      string(APPEND identity "${input}:${size}:${${input}};")
    endforeach()
  endforeach()
  string(SHA256 stamp "${identity}")
  set(${output} "${stamp}" PARENT_SCOPE)
endfunction()
