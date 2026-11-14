# See the LICENSE file at the top of the project tree for copyright
# and license details.
# Prepared compile-free contract; execution belongs to manual CI.
include("${PROJECT_ROOT}/build/cmake/OpenSSLProbeStamp.cmake")

# Fixture inputs must not leak into the including source checker.
function(check_openssl_probe_stamp_fixture)

  # Keep headers/version identical while selecting different libraries.
  set(OPENSSL_INCLUDE_DIR "/fixture/include")
  set(OPENSSL_SSL_LIBRARY "/fixture/shared/libssl")
  set(OPENSSL_CRYPTO_LIBRARY "/fixture/shared/libcrypto")
  set(I2PD_OPENSSL_KIND "OpenSSL")
  set(I2PD_OPENSSL_VERSION "fixture-version")
  i2pd_openssl_probe_stamp(baseline)
  i2pd_openssl_probe_stamp(unchanged)
  if(NOT baseline STREQUAL unchanged)
    message(FATAL_ERROR "Unchanged OpenSSL probes need stable cache identity")
  endif()
  foreach(input IN ITEMS OPENSSL_SSL_LIBRARY OPENSSL_CRYPTO_LIBRARY
      OPENSSL_USE_STATIC_LIBS OPENSSL_MSVC_STATIC_RT CMAKE_REQUIRED_LINK_OPTIONS
      CMAKE_REQUIRED_FLAGS CMAKE_REQUIRED_DEFINITIONS CMAKE_CXX_FLAGS
      CMAKE_EXE_LINKER_FLAGS CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_CXX_COMPILER_TARGET
      CMAKE_SYSROOT CMAKE_CXX_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_RELEASE
      CMAKE_BUILD_TYPE CMAKE_TRY_COMPILE_CONFIGURATION)
    set(saved "${${input}}")
    set(${input} "${saved};changed-input")
    i2pd_openssl_probe_stamp(changed)
    if(changed STREQUAL baseline)
      message(FATAL_ERROR "OpenSSL probe identity ignores ${input}")
    endif()
    set(${input} "${saved}")
  endforeach()
  # Separator-containing fields must not alias across library boundaries.
  set(OPENSSL_SSL_LIBRARY "ssl|crypto")
  set(OPENSSL_CRYPTO_LIBRARY "tail")
  i2pd_openssl_probe_stamp(left)
  set(OPENSSL_SSL_LIBRARY "ssl")
  set(OPENSSL_CRYPTO_LIBRARY "crypto|tail")
  i2pd_openssl_probe_stamp(right)
  if(left STREQUAL right)
    message(FATAL_ERROR "OpenSSL probe identity fields are ambiguous")
  endif()

  set(OPENSSL_SSL_LIBRARY "ssl;crypto")
  set(OPENSSL_CRYPTO_LIBRARY "tail")
  i2pd_openssl_probe_stamp(left)
  set(OPENSSL_SSL_LIBRARY "ssl")
  set(OPENSSL_CRYPTO_LIBRARY "crypto;tail")
  i2pd_openssl_probe_stamp(right)
  if(left STREQUAL right)
    message(FATAL_ERROR "OpenSSL probe identity list fields are ambiguous")
  endif()
endfunction()
check_openssl_probe_stamp_fixture()
