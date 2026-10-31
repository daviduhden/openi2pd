#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: verify java25.
#
# Runs the compiled probe on the build JDK and, when both R8_JAR and
# ANDROID_JAR are set, dexes the classes with D8 and R8 at --min-api 29
# and prints the DEX magic; supplying only one of the two jars is an
# error.
#
# Contract: JAVAC, JAVA and JAVAP override the tools; otherwise they are
# taken from JAVA_HOME (when it contains bin/javac) and then from PATH.
# The probe must compile with --release 25 and produce class-file major
# 69, but it runs on the build JDK, so a successful run is not ART
# evidence. All artefacts stay in a temporary directory below TMPDIR
# (default /tmp) that the EXIT/HUP/INT/TERM trap removes; the source
# tree is never written. Any failed check exits 1, the D8/R8 stage is
# skipped when neither jar is supplied, and the script itself verifies
# the DEX version bytes (039) instead of trusting the tool exit status.
#

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
source_file="$script_dir/src/org/openi2pd/java25probe/Java25Probe.java"
keep_file="$script_dir/keep.pro"

if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/javac" ]; then
	javac_bin="${JAVAC:-$JAVA_HOME/bin/javac}"
	java_bin="${JAVA:-$JAVA_HOME/bin/java}"
	javap_bin="${JAVAP:-$JAVA_HOME/bin/javap}"
else
	javac_bin="${JAVAC:-javac}"
	java_bin="${JAVA:-java}"
	javap_bin="${JAVAP:-javap}"
fi

echo "javac: $("$javac_bin" -version 2>&1)"
echo "java:  $("$java_bin" -version 2>&1 | head -n 1)"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/open-i2pd-java25-probe.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

classes_dir="$work_dir/classes"
dex_dir="$work_dir/dex"
r8_dir="$work_dir/r8"
mkdir -p "$classes_dir" "$dex_dir" "$r8_dir"

echo
echo "== javac --release 25 =="
"$javac_bin" --release 25 -d "$classes_dir" "$source_file"

major=$("$javap_bin" -verbose "$classes_dir/org/openi2pd/java25probe/Java25Probe.class" |
	awk '/major version/ { print $3; exit }')
echo "Java25Probe.class major version: $major"
if [ "$major" != "69" ]; then
	echo "ERROR: expected class-file major version 69 (Java 25), got '$major'" >&2
	exit 1
fi

echo
echo "== run on the build JDK (not ART) =="
"$java_bin" -cp "$classes_dir" org.openi2pd.java25probe.Java25Probe

if [ -n "${R8_JAR:-}" ] || [ -n "${ANDROID_JAR:-}" ]; then
	if [ -z "${R8_JAR:-}" ] || [ -z "${ANDROID_JAR:-}" ]; then
		echo "ERROR: set both R8_JAR and ANDROID_JAR, or neither" >&2
		exit 1
	fi
	if [ ! -f "$R8_JAR" ]; then
		echo "ERROR: R8_JAR not found: $R8_JAR" >&2
		exit 1
	fi
	if [ ! -f "$ANDROID_JAR" ]; then
		echo "ERROR: ANDROID_JAR not found: $ANDROID_JAR" >&2
		exit 1
	fi

	# Dex every probe class, including the nested record/sealed classes.
	# Passing only the outer class left the record and type-switch lowering
	# paths untested while the keep rule referenced classes D8 never saw.
	# Probe class names cannot contain newlines; split only on the find
	# output line separator and disable pathname expansion so spaces stay
	# within paths.
	class_files=$(find "$classes_dir" -name '*.class' -print)
	class_files_ifs=$IFS
	IFS='
'
	set -f
	# shellcheck disable=SC2086
	set -- $class_files
	set +f
	IFS=$class_files_ifs

	echo
	echo "== D8 --min-api 29 =="
	"$java_bin" -cp "$R8_JAR" com.android.tools.r8.D8 \
		--min-api 29 \
		--lib "$ANDROID_JAR" \
		--output "$dex_dir" \
		"$@"
	ls -l "$dex_dir/classes.dex"

	echo
	echo "== R8 --min-api 29 =="
	"$java_bin" -cp "$R8_JAR" com.android.tools.r8.R8 \
		--min-api 29 \
		--lib "$ANDROID_JAR" \
		--pg-conf "$keep_file" \
		--output "$r8_dir" \
		"$@"
	ls -l "$r8_dir/classes.dex"

	echo
	echo "== DEX magic =="
	if command -v od >/dev/null 2>&1; then
		od -An -c -N8 "$dex_dir/classes.dex"
		od -An -c -N8 "$r8_dir/classes.dex"
		# minSdk 29 must produce DEX version 039, not the older 037 used by
		# historical --min-api 24 probes.
		for dex in "$dex_dir/classes.dex" "$r8_dir/classes.dex"; do
			version=$(od -An -t u1 -j4 -N3 "$dex" | tr -d ' \n')
			if [ "$version" != "485157" ]; then # '039' in ASCII (48/51/57)
				echo "ERROR: $dex is not DEX 039 (got $version)" >&2
				exit 1
			fi
		done
	fi
else
	echo
	echo "D8/R8: skipped (set R8_JAR and ANDROID_JAR to run the --min-api 29 checks)"
fi

echo
echo "OK: Java 25 language probe compiled successfully."
