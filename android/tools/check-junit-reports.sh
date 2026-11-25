#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Validate the JUnit XML emitted by Gradle or Android instrumentation.
set -eu

dir=${1:?JUnit report directory required}
executed=false
for report in "$dir"/TEST-*.xml; do
    if [ ! -f "$report" ]; then
        echo "Missing JUnit reports in $dir" >&2
        exit 1
    fi
    # Both writers expose suite counters and direct testcase children.
    # Read only structural elements; stderr and stdout can quote XML.
    valid=$(xmllint --nonet --xpath 'boolean(
      /*[self::testsuite or self::testsuites] and //testsuite
      and not(//testsuite[
        not(number(@tests) = count(testcase))
        or (@skipped and not(number(@skipped) = count(testcase/skipped)))
        or not(number(@failures) = 0 and number(@errors) = 0)])
      and not(/testsuites[
        not(number(@tests) = count(testsuite/testcase))
        or (@skipped and not(number(@skipped) = count(testsuite/testcase/skipped)))
        or not(number(@failures) = 0 and number(@errors) = 0)])
      and not(//failure or //error))' "$report")
    if [ "$valid" != true ]; then
        echo "Invalid suite counts or failed tests in $report" >&2
        exit 1
    fi
    ran=$(xmllint --nonet --xpath 'boolean(//testsuite/testcase[not(skipped)])' "$report")
    if [ "$ran" = true ]; then
        executed=true
    fi
done
if [ "$executed" != true ]; then
    echo "No executed test cases in $dir" >&2
    exit 1
fi
