# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: arm64 android.
#
# vcpkg Android arm64 overlay triplet: inherit the upstream NDK triplet and
# request release-only dependency builds for the manual Android CI graph.
#

include("$ENV{VCPKG_ROOT}/triplets/arm64-android.cmake")
# Upstream's Android triplet pins API 28, while the application's Android 10
# floor is API 29. Build every dependency against API 29 so the JNI link and
# all packaged native libraries share the supported libc/fortify surface.
set(VCPKG_CMAKE_SYSTEM_VERSION 29)
set(VCPKG_BUILD_TYPE release)
