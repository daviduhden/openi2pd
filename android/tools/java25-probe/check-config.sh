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

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
app_gradle="$root/android/app/build.gradle"
root_gradle="$root/android/build.gradle"
wrapper="$root/android/gradle/wrapper/gradle-wrapper.properties"
properties="$root/android/gradle.properties"

fail()
{
    echo "Android Java 25 config check failed: $*" >&2
    exit 1
}

grep -Fq 'sourceCompatibility = JavaVersion.VERSION_25' "$app_gradle" \
    || fail 'sourceCompatibility is not Java 25'
grep -Fq 'targetCompatibility = JavaVersion.VERSION_25' "$app_gradle" \
    || fail 'targetCompatibility is not Java 25'
grep -Fq 'languageVersion = JavaLanguageVersion.of(25)' "$app_gradle" \
    || fail 'Java toolchain is not exact JDK 25'
grep -Fq 'compileSdk 37' "$app_gradle" || fail 'compileSdk is not 37'
grep -Fq "buildToolsVersion '36.0.0'" "$app_gradle" || fail 'Build Tools pin is not 36.0.0'
grep -Fq 'targetSdk 37' "$app_gradle" || fail 'targetSdk is not 37'
grep -Fq 'minSdk 24' "$app_gradle" || fail 'minSdk is not 24'
grep -Fq '30.0.16248370' "$app_gradle" || fail 'NDK r30 LTS pin is missing'
grep -Fq "classpath 'com.android.tools.build:gradle:9.4.1'" "$root_gradle" \
    || fail 'AGP pin is not 9.4.1'
grep -Fq 'gradle-9.8.1-bin.zip' "$wrapper" || fail 'Gradle wrapper is not 9.8.1'
grep -Fq 'org.gradle.java.installations.auto-download=false' "$properties" \
    || fail 'implicit JDK download is not disabled'
grep -Fq 'android.builtInKotlin=false' "$properties" \
    || fail 'AGP bundled-Kotlin target-25 workaround is missing'
[ -d "$root/android/app/src/main/assets/certificates" ] \
    || fail 'certificates asset symlink/target is missing or dangling'

if grep -Fq 'JavaVersion.VERSION_17' "$app_gradle" \
    || grep -Fq 'JavaLanguageVersion.of(17)' "$app_gradle" \
    || grep -Fq 'jvmTarget = "17"' "$app_gradle"; then
    fail 'Java level was silently reduced to 17'
fi

# The graceful-stop deadline must be owned by RouterService and enqueued on
# its worker; a java.util.Timer would call JNI from a timer thread.
if grep -RInE 'java\.util\.Timer|TimerTask' "$root/android/app/src/main/java"; then
    fail 'graceful shutdown uses a java.util.Timer instead of the service worker'
fi
grep -Fq 'State.stopping' "$root/android/app/src/main/java/org/openi2pd/router/DaemonWrapper.java" \
    || fail 'DaemonWrapper has no explicit stopping state'
grep -Fq 'gracefulStopExpired' "$root/android/app/src/main/java/org/openi2pd/router/RouterService.java" \
    || fail 'RouterService does not schedule the graceful-stop deadline'

# These JDK APIs are not provided by Android's java.* bootclasspath. Keep them
# out of app source; the Java 25 probe is intentionally outside this scan.
if grep -RInE 'java\.lang\.foreign|java\.lang\.invoke|java\.lang\.classfile|java\.lang\.ScopedValue|java\.lang\.StableValue|java\.util\.Sequenced|javax\.crypto\.KDF|java\.security\.PEMEncoder|java\.security\.PEMDecoder|Thread\.ofVirtual|StructuredTaskScope|jdk\.incubator\.vector' \
    "$root/android/app/src/main/java"; then
    fail 'app source references a JDK-only API that is unavailable on ART'
fi

echo "Android Java 25 config check passed"
