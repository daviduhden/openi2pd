#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check config.
#
# Also asserts that the certificates asset, the bundled-Kotlin workaround and
# the RouterService-owned graceful-stop path are present, and fails on any
# app-source reference to a JDK-only API that ART does not provide.
#

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$script_dir/../../.." && pwd)
app_gradle="$root/android/app/build.gradle"
root_gradle="$root/android/build.gradle"
wrapper="$root/android/gradle/wrapper/gradle-wrapper.properties"
properties="$root/android/gradle.properties"
native_cmake="$root/android/app/src/main/cpp/CMakeLists.txt"
version_header="$root/libi2pd/version.h"

fail() {
	echo "Android Java 25 config check failed: $*" >&2
	exit 1
}

grep -Fq 'sourceCompatibility = JavaVersion.VERSION_25' "$app_gradle" ||
	fail 'sourceCompatibility is not Java 25'
grep -Fq 'targetCompatibility = JavaVersion.VERSION_25' "$app_gradle" ||
	fail 'targetCompatibility is not Java 25'
grep -Fq 'languageVersion = JavaLanguageVersion.of(25)' "$app_gradle" ||
	fail 'Java toolchain is not exact JDK 25'
grep -Fq 'compileSdk 37' "$app_gradle" || fail 'compileSdk is not 37'
grep -Fq "buildToolsVersion = '36.0.0'" "$app_gradle" || fail 'Build Tools pin is not 36.0.0'
grep -Fq 'targetSdk 37' "$app_gradle" || fail 'targetSdk is not 37'
grep -Fq 'minSdk 29' "$app_gradle" || fail 'minSdk is not 29'
grep -Fq '30.0.16248370' "$app_gradle" || fail 'NDK r30 LTS pin is missing'
grep -Fq 'ndkVersion = ' "$app_gradle" || fail 'ndkVersion is not declared on the Android extension'
grep -Fq "classpath 'com.android.tools.build:gradle:9.4.1'" "$root_gradle" ||
	fail 'AGP pin is not 9.4.1'
grep -Fq 'gradle-9.8.1-bin.zip' "$wrapper" || fail 'Gradle wrapper is not 9.8.1'
grep -Fq 'org.gradle.java.installations.auto-download=false' "$properties" ||
	fail 'implicit JDK download is not disabled'
grep -Fq 'org.gradle.jvmargs=-Xmx' "$properties" ||
	fail 'Gradle daemon heap is not bounded'
grep -Fq 'org.gradle.workers.max=' "$properties" ||
	fail 'Gradle worker count is not bounded'
grep -Fq 'version "3.31.6"' "$app_gradle" ||
	fail 'SDK CMake version pin is not 3.31.6'
grep -Fq '"-DANDROID_STL=c++_shared"' "$app_gradle" ||
	fail 'JNI build is not aligned with the c++_shared vcpkg STL'
grep -Fq '"-DI2PD_OPENSSL_EXPECT=OpenSSL"' "$app_gradle" ||
	fail 'JNI build does not pin the expected OpenSSL backend'
grep -Fq 'CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH' "$native_cmake" ||
	fail 'wrapper does not allow dependency prefixes outside the NDK sysroot'
grep -Fq 'CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH' "$native_cmake" ||
	fail 'wrapper does not allow headers outside the NDK sysroot'
grep -Fq 'CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH' "$native_cmake" ||
	fail 'wrapper does not allow packages outside the NDK sysroot'
grep -Fq 'i2pd.abis' "$app_gradle" ||
	fail 'ABI list is not property-driven'
grep -Fq 'arm64-v8a' "$app_gradle" || fail 'default ABI is not arm64-v8a'
grep -Fq 'android.builtInKotlin=false' "$properties" ||
	fail 'AGP bundled-Kotlin target-25 workaround is missing'
[ -d "$root/android/app/src/main/assets/certificates" ] ||
	fail 'certificates asset symlink/target is missing or dangling'
[ -f "$root/android/app/src/main/assets/i2pd.conf" ] ||
	fail 'bundled i2pd.conf asset is missing'
certificates=$(find "$root/contrib/certificates" -type f -name '*.crt' | wc -l | tr -d ' ')
[ "$certificates" -gt 0 ] || fail 'no reseed/family certificates are available to package'

major=$(sed -n 's/^#define I2PD_VERSION_MAJOR \([0-9][0-9]*\)$/\1/p' "$version_header")
minor=$(sed -n 's/^#define I2PD_VERSION_MINOR \([0-9][0-9]*\)$/\1/p' "$version_header")
micro=$(sed -n 's/^#define I2PD_VERSION_MICRO \([0-9][0-9]*\)$/\1/p' "$version_header")
[ -n "$major" ] && [ -n "$minor" ] && [ -n "$micro" ] ||
	fail 'cannot read the project version from libi2pd/version.h'
expected_name="$major.$minor.$micro"
expected_code=$(( (major * 100 + minor) * 10000 + micro * 100 ))
grep -Fq "versionName \"$expected_name\"" "$app_gradle" ||
	fail "Android versionName does not match $expected_name"
grep -Fq "versionCode $expected_code" "$app_gradle" ||
	fail "Android versionCode does not match $expected_code"
grep -Fq "i2pdBaseVersionCode = $expected_code" "$app_gradle" ||
	fail 'per-ABI base versionCode does not match the project version'

# Any older release is a silent regression: Java 25 is the project target,
# not a best-effort option that may fall back to 17/21.
for older in 8 11 17 21 23 24; do
	if grep -Fq "JavaVersion.VERSION_$older" "$app_gradle" ||
		grep -Fq "JavaLanguageVersion.of($older)" "$app_gradle" ||
		grep -Fq "jvmTarget = \"$older\"" "$app_gradle"; then
		fail "Java level was silently reduced to $older"
	fi
done
grep -Fq 'android.builtInKotlin=false' "$properties" ||
	fail 'built-in Kotlin workaround is missing (Java 25 jvmTarget would fail task creation)'
grep -Fq -- '--min-api 29' "$root/android/tools/java25-probe/verify-java25.sh" ||
	fail 'Java 25 D8/R8 probe does not match minSdk 29'

# The graceful-stop deadline must be owned by RouterService and enqueued on
# its worker; a java.util.Timer would call JNI from a timer thread.
if grep -RInE 'java\.util\.Timer|TimerTask' "$root/android/app/src/main/java"; then
	fail 'graceful shutdown uses a java.util.Timer instead of the service worker'
fi
grep -Fq 'State.stopping' "$root/android/app/src/main/java/org/openi2pd/router/DaemonWrapper.java" ||
	fail 'DaemonWrapper has no explicit stopping state'
grep -Fq 'gracefulStopExpired' "$root/android/app/src/main/java/org/openi2pd/router/RouterService.java" ||
	fail 'RouterService does not schedule the graceful-stop deadline'

# These JDK APIs are not provided by Android's java.* bootclasspath. Keep them
# out of app and instrumentation source; the Java 25 probe is outside this scan.
for source_dir in "$root/android/app/src/main/java" "$root/android/app/src/androidTest/java"; do
	if grep -RInE 'java\.lang\.foreign|java\.lang\.invoke|java\.lang\.classfile|java\.lang\.ScopedValue|java\.lang\.StableValue|java\.util\.Sequenced|javax\.crypto\.KDF|java\.security\.PEMEncoder|java\.security\.PEMDecoder|Thread\.ofVirtual|Thread\.startVirtualThread|Thread\.ofPlatform|Executors\.newVirtualThreadPerTaskExecutor|StructuredTaskScope|java\.util\.concurrent\.Flow|HexFormat|java\.lang\.MatchException|jdk\.incubator\.vector' \
		"$source_dir"; then
		fail 'app or instrumentation source references a JDK-only API that is unavailable on ART'
	fi
	# Collection/String convenience APIs exist only on newer Android releases.
	# Unit tests under src/test run on the build JDK and are intentionally
	# exempt; main and instrumentation sources run on the API 29 floor.
	if grep -RInE '(List|Map|Set)\.(of|copyOf)\(|Stream\.toList\(|\.toList\(\)|Optional\.isEmpty\(|String\.(repeat|isBlank|strip|lines|formatted)\(' \
		"$source_dir"; then
		fail 'app or instrumentation source uses a collection/String API above the API 29 floor'
	fi
done

echo "Android Java 25 config check passed"
