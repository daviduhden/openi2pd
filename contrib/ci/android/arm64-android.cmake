# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: arm64 android.
#
# vcpkg Android arm64 overlay triplet: inherit the upstream NDK triplet and
# request release-only dependency builds for the manual Android CI graph.
#

include("$ENV{VCPKG_ROOT}/triplets/arm64-android.cmake")
# Upstream's Android triplet pins API 28. The application's minSdk is 24, so
# build every dependency against API 24; otherwise fortified libc symbols such
# as __sendto_chk (API 26+) remain unresolved in the final JNI link.
set(VCPKG_CMAKE_SYSTEM_VERSION 24)
set(VCPKG_BUILD_TYPE release)
