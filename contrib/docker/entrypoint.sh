#!/bin/sh
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: entrypoint.
#
# Container entrypoint that links the mounted certificate directory into the
# data directory, forwards extra arguments to i2pd and execs the router.
# --help runs the router's own help without starting it.
#

COMMAND=/usr/local/bin/i2pd
# DATA_DIR and DEFAULT_ARGS are supplied by the image ENV and may be overridden
# by the container operator. The runtime user needs write access to DATA_DIR.

if [ "$1" = "--help" ]; then
	set -- "$COMMAND" --help
else
	# The data directory is a persisted volume; do not fail or duplicate the
	# link when the container is restarted with the same volume.
	[ -e "$DATA_DIR/certificates" ] || [ -L "$DATA_DIR/certificates" ] ||
		ln -s /i2pd_certificates "$DATA_DIR/certificates"
	# No errexit is set: a failed link attempt does not itself stop startup.
	# DEFAULT_ARGS is a shell word list; field splitting and pathname expansion
	# apply to its unquoted contents. Explicit arguments remain separate in "$@".
	# shellcheck disable=SC2086
	set -- "$COMMAND" $DEFAULT_ARGS "$@"
fi

# Replace the entrypoint shell so the router receives container stop signals
# directly; no shell remains to relay them or reap the router as a child.
exec "$@"
