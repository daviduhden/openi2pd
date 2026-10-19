# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: arm64 android.
#
# vcpkg Android arm64 overlay triplet: inherit the upstream NDK triplet and
# request release-only dependency builds for the manual Android CI graph.
#

include("$ENV{VCPKG_ROOT}/triplets/arm64-android.cmake")
set(VCPKG_BUILD_TYPE release)
