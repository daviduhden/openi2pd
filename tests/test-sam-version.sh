#!/usr/bin/env bash
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: test sam version.
#
# Drives the SAM bridge on 127.0.0.1:7656 with MIN/MAX version ranges.
# Expects OK VERSION=3.3 or NOVERSION for reversed, malformed and
# out-of-range values.
#
# Manual probe, not CTest. It requires a router with the SAM bridge
# enabled on loopback, netcat with `-q 0` (half-close), and Bash arrays.
# It reads no router files/configuration and runs unprivileged.
#
# Each case sends one `HELLO VERSION ...` line and compares the full
# `HELLO REPLY RESULT=...` response. Exit status is 0 only if all cases
# match; otherwise it returns 1. There is no retry; start and stop the
# router around the probe. Keep SAM loopback-only because it is a local
# administrative surface.

# This script tests the version negotiation in the SAM handshake.

# Inputs and expected outputs
IN=()
EXP=()

IN+=("MIN=3.1 MAX=3.3")
EXP+=("OK VERSION=3.3")

IN+=("MAX=3.3")
EXP+=("OK VERSION=3.3")

IN+=("MAX=3.4")
EXP+=("OK VERSION=3.3")

IN+=("MIN=3.0")
EXP+=("OK VERSION=3.3")

IN+=("MIN=3.1")
EXP+=("OK VERSION=3.3")

IN+=("MIN=2.9")
EXP+=("OK VERSION=3.3")

IN+=("")
EXP+=("OK VERSION=3.3")

IN+=("MIN=3.3 MAX=3.1")
EXP+=("NOVERSION")

IN+=("MIN=3.5 MAX=2.7")
EXP+=("NOVERSION")

IN+=("MIN=2.7 MAX=3.5")
EXP+=("OK VERSION=3.3")

IN+=("MIN=afddab3vsfdsg1df MAX=dsaaffdb3ggfgfbgf1bssbf")
EXP+=("NOVERSION")

# A bound without a dot matches no candidate; SAMHandlerFactory answers
# NOVERSION. The former local comparator treated "31" as "3.1".
IN+=("MIN=31 MAX=31")
EXP+=("NOVERSION")

failed=0
i=0
while [ "$i" -lt "${#IN[@]}" ]; do
	printf 'HELLO VERSION %s - ' "${IN[$i]}"

	# Observed output
	OBS=$(printf 'HELLO VERSION %s\n' "${IN[$i]}" | nc -q 0 127.0.0.1 7656)

	if [ "$OBS" = "HELLO REPLY RESULT=${EXP[$i]}" ]; then
		printf "OK\n"
	else
		printf 'received %s\n' "$OBS"
		failed=1
	fi
	i=$((i + 1))
done

# A printed mismatch must fail the manual live-bridge invocation.
exit "$failed"
