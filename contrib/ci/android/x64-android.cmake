# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: x64 android.
#
# vcpkg's x64 triplet selects Android's x86_64 ABI. Keep that upstream
# mapping and override its API 28 dependency floor after including it.
#

include("$ENV{VCPKG_ROOT}/triplets/x64-android.cmake")
set(VCPKG_CMAKE_SYSTEM_VERSION 29)
set(VCPKG_BUILD_TYPE release)
