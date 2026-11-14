#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check config.
#
# Also asserts that the certificates asset, the bundled-Kotlin
# workaround and the RouterService-owned graceful-stop path are present,
# fails on any app-source reference to a JDK-only API that ART does not
# provide, and requires every metric label TextView to name its value
# view through android:labelFor.
#
# Contract: a static, grep-only audit of the checked-in Android and
# native build configuration. It needs no JDK, Android SDK/NDK, Gradle
# or network access, and it is run from the manual Android job on a
# clean checkout. Every path it reads is derived from this script's
# location, so it is safe to invoke from any working directory. It
# writes nothing and fails on the first violated contract with exit 1
# and a diagnostic on stderr; it does not build, dex or run on a device.
# The denylist self-test pipes literal samples through the same
# expressions, so it proves the patterns reject the call shape without
# rejecting local identifiers.
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
layout_dir="$root/android/app/src/main/res/layout"

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
grep -Fq 'project(OpenI2PdAndroid LANGUAGES C CXX)' "$native_cmake" ||
	fail 'Android native entry point has no explicit project declaration'
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
if [ -z "$major" ] || [ -z "$minor" ] || [ -z "$micro" ]; then
	fail 'cannot read the project version from libi2pd/version.h'
fi
expected_name="$major.$minor.$micro"
expected_code=$(((major * 100 + minor) * 10000 + micro * 100))
grep -Fq "versionName \"$expected_name\"" "$app_gradle" ||
	fail "Android versionName does not match $expected_name"
grep -Fq "versionCode $expected_code" "$app_gradle" ||
	fail "Android versionCode does not match $expected_code"
grep -Fq "i2pdBaseVersionCode = $expected_code" "$app_gradle" ||
	fail 'per-ABI base versionCode does not match the project version'

# Any older release is a silent regression: Java 25 is the project
# target, not a best-effort option that may fall back to 17/21.
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
# Standalone dexing must use AGP's resolved artifact, never cache order.
grep -Fq "tasks.register('writeJava25R8Classpath')" "$root_gradle" ||
	fail 'AGP R8 provenance task is missing'
grep -Fq 'com.android.build.gradle.AppPlugin.class.classLoader' "$root_gradle" ||
	fail 'R8 provenance does not use the AGP plugin classloader'
grep -Fq "java25AgpLoader.loadClass('com.android.tools.r8.D8').protectionDomain.codeSource.location.toURI()" "$root_gradle" ||
	fail 'R8 report does not use the AGP-loaded D8 container'
grep -Fq "java25AgpLoader.loadClass('com.android.tools.r8.R8').protectionDomain.codeSource.location.toURI()" "$root_gradle" ||
	fail 'R8 report does not inspect the AGP-loaded R8 container'
grep -Fq 'java25D8Container.canonicalFile != java25R8Container.canonicalFile' "$root_gradle" ||
	fail 'D8 and R8 provenance does not reject mismatched containers'

# The graceful-stop deadline must be owned by RouterService and enqueued
# on its worker; a java.util.Timer would call JNI from a timer thread.
if grep -RInE 'java\.util\.Timer|TimerTask' "$root/android/app/src/main/java"; then
	fail 'graceful shutdown uses a java.util.Timer instead of the service worker'
fi
grep -Fq 'State.stopping' "$root/android/app/src/main/java/org/openi2pd/router/DaemonWrapper.java" ||
	fail 'DaemonWrapper has no explicit stopping state'
grep -Fq 'gracefulStopExpired' "$root/android/app/src/main/java/org/openi2pd/router/RouterService.java" ||
	fail 'RouterService does not schedule the graceful-stop deadline'

# These JDK APIs are not provided by Android's java.* bootclasspath.
# Keep them out of app and instrumentation source; the Java 25 probe is
# outside this scan.
jdk_only_pattern='java\.lang\.foreign|java\.lang\.invoke|java\.lang\.classfile|java\.lang\.ScopedValue|java\.lang\.StableValue|java\.util\.Sequenced|javax\.crypto\.KDF|java\.security\.PEMEncoder|java\.security\.PEMDecoder|Thread\.ofVirtual|Thread\.startVirtualThread|Thread\.ofPlatform|Executors\.newVirtualThreadPerTaskExecutor|StructuredTaskScope|java\.util\.concurrent\.Flow|HexFormat|java\.lang\.MatchException|java\.util\.random\.RandomGenerator|jdk\.incubator\.vector'
# Collection/String convenience APIs exist only on newer Android
# releases. Unit tests under src/test run on the build JDK and are
# intentionally exempt; main and instrumentation sources run on the API
# 29 floor. The list is per method name because the class-level factory
# names (`Map.of` vs `Map.ofEntries`) and the String strip variants are
# different API levels.
collection_pattern='(List|Map|Set)\.(of|ofEntries|copyOf)\(|Stream\.(toList|ofNullable|takeWhile|dropWhile|mapMulti)\(|\.toList\(\)|Optional\.(isEmpty|or|stream)\(|Collectors\.toUnmodifiable|Objects\.requireNonNullElse(Get)?\('
# These are instance methods: requiring a String class receiver misses
# real calls such as text.repeat(3) or (prefix + suffix).strip(). Match
# the member-call shape, including whitespace before the parenthesis;
# unqualified local helpers such as repeat(text, count) remain valid.
# This is a conservative source guard, not receiver-type resolution.
# Leave ambiguous names to Android Lint: BufferedReader.lines() is
# supported at the API 29 floor, and other types may define transform.
string_api_pattern='\.(repeat|isBlank|strip|stripLeading|stripTrailing|stripIndent|indent|translateEscapes|formatted)[[:space:]]*\('
# Java 21 additions that arrive through SequencedCollection when the
# compile classpath exposes JDK 25, plus Java 19/21 platform methods
# absent from API 29: List.getFirst/getLast/addFirst/addLast/
# removeFirst/removeLast/reversed, String.stripIndent, Math.clamp and
# Thread.threadId. Match the call shape (a dot and an opening
# parenthesis) so an identifier such as `reversed` or a helper named
# `getFirstValue()` is not rejected. Android's Deque/ArrayDeque/
# LinkedList first/last helpers predate API 29, but a receiver typed as
# List or Collection cannot call them there, so the call shape stays
# refused and `peekFirst`/`pollFirst`/`offerFirst` are the approved
# replacements.
java21_api_pattern='\.(getFirst|getLast|addFirst|addLast|removeFirst|removeLast)\(|\.reversed\(\)|\.stripIndent\(|Math\.clamp\(|\.threadId\('

for source_dir in "$root/android/app/src/main/java" "$root/android/app/src/androidTest/java"; do
	if grep -RInE "$jdk_only_pattern" "$source_dir"; then
		fail 'app or instrumentation source references a JDK-only API that is unavailable on ART'
	fi
	if grep -RInE "$collection_pattern" "$source_dir"; then
		fail 'app or instrumentation source uses a collection API above the API 29 floor'
	fi
	if grep -RInE "$string_api_pattern" "$source_dir"; then
		fail 'app or instrumentation source uses a String instance API above the API 29 floor'
	fi
	if grep -RInE "$java21_api_pattern" "$source_dir"; then
		fail 'app or instrumentation source uses a Java 19/21 API above the API 29 floor'
	fi
done

# Negative fixture: the same expressions must stay silent for ordinary
# identifiers and unrelated methods, so a clean tree is not the only
# evidence that the denylist is precise.
denylist_selftest() {
	pattern=$1
	expect=$2
	sample=$3
	if printf '%s\n' "$sample" | grep -qE "$pattern"; then
		matched=yes
	else
		matched=no
	fi
	[ "$matched" = "$expect" ] ||
		fail "denylist self-test: expected $expect for: $sample"
}

for sample in \
	'"x".repeat(3);' \
	'(prefix + suffix).repeat (3);' \
	'text.isBlank();' \
	'text.strip();' \
	'text.stripLeading();' \
	'text.stripTrailing();' \
	'text.stripIndent();' \
	'text.indent(2);' \
	'text.translateEscapes();' \
	'text.formatted(value);'; do
	denylist_selftest "$string_api_pattern" yes "$sample"
done
for sample in \
	'repeat(text, count);' \
	'private static String repeat(String text, int count) {' \
	'text.repeatValue(3);' \
	'boolean isBlank = false;' \
	'reader.lines();' \
	'matrix.transform(point);' \
	'text.stripIndentation();' \
	'String.format(pattern, value);'; do
	denylist_selftest "$string_api_pattern" no "$sample"
done

for sample in \
	'orders.getFirst();' \
	'orders.getLast();' \
	'queue.addFirst(node);' \
	'queue.addLast(node);' \
	'queue.removeFirst();' \
	'queue.removeLast();' \
	'orders.reversed();' \
	'line.stripIndent();' \
	'Math.clamp(port, 1, 65535);' \
	'Thread.currentThread().threadId();'; do
	denylist_selftest "$java21_api_pattern" yes "$sample"
done
for sample in \
	'int reversed = orders.size();' \
	'String getFirstValue() { return null; }' \
	'list.reversedOrder();' \
	'text.stripIndentation();' \
	'Math.max(port, 1);' \
	'long threadIdentifier = 0L;'; do
	denylist_selftest "$java21_api_pattern" no "$sample"
done

# Every metric label TextView must name its value view, otherwise a
# screen reader reads a bare number with no unit. The walk requires the
# label TextView to be the element immediately before the value, that
# its android:labelFor targets that value and that every labelFor in the
# layout resolves to an id declared in the same layout. Plain TextViews
# with no id are unaffected.
check_label_for() {
	layout=$1
	shift
	awk -v layout="$layout" -v expected="$*" '
	BEGIN {
		RS = "<"
		bad = 0
		count = split(expected, want, " ")
		for (i = 1; i <= count; i++) pending[want[i]] = 1
		previous = ""
	}
	{
		id = ""
		label = ""
		if (match($0, /android:id="@\+id\/[A-Za-z0-9_]+"/)) {
			id = substr($0, RSTART, RLENGTH)
			sub(/^.*@\+id\//, "", id)
			sub(/".*$/, "", id)
			declared[id] = 1
		}
		if ($0 ~ /^TextView([ \t\r\n]|$)/ &&
				match($0, /android:labelFor="@id\/[A-Za-z0-9_]+"/)) {
			label = substr($0, RSTART, RLENGTH)
			sub(/^.*@id\//, "", label)
			sub(/".*$/, "", label)
			targets[label] = 1
		}
		if (id != "" && (id in pending)) {
			if (previous != id) {
				printf "%s: @id/%s is not labelled by the preceding TextView\n", layout, id
				bad = 1
			}
			seen[id] = 1
		}
		if ($0 ~ /^TextView([ \t\r\n]|$)/)
			previous = label
		else
			previous = ""
	}
	END {
		for (i = 1; i <= count; i++) {
			if (!(want[i] in seen)) {
				printf "%s: no android:labelFor for @id/%s\n", layout, want[i]
				bad = 1
			}
		}
		for (target in targets) {
			if (!(target in declared)) {
				printf "%s: android:labelFor=\"@id/%s\" does not resolve\n", layout, target
				bad = 1
			}
		}
		exit bad
	}
	' "$layout"
}

check_label_for "$layout_dir/fragment_dashboard.xml" \
	metric_uptime metric_peers metric_routers metric_tunnels \
	metric_download metric_upload metric_reseed ||
	fail 'dashboard metric labels are not associated with their values'
check_label_for "$layout_dir/fragment_statistics.xml" \
	stat_download_rate stat_upload_rate stat_total_received stat_total_sent \
	stat_total_transit stat_tunnels stat_uptime stat_peers stat_routers ||
	fail 'statistics metric labels are not associated with their values'
check_label_for "$layout_dir/fragment_tunnels.xml" \
	text_tunnels_in text_tunnels_out text_tunnels_transit text_tunnels_total ||
	fail 'tunnel metric labels are not associated with their values'
check_label_for "$layout_dir/fragment_network.xml" text_ipv4 text_ipv6 ||
	fail 'network address labels are not associated with their values'

echo "Android Java 25 config check passed"
