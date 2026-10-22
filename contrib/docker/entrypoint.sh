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
# To make ports exposeable
# Note: $DATA_DIR is defined in /etc/profile

if [ "$1" = "--help" ]; then
	set -- "$COMMAND" --help
else
	ln -s /i2pd_certificates "$DATA_DIR"/certificates
	# DEFAULT_ARGS is intentionally a shell word list supplied by the image environment.
	# shellcheck disable=SC2086
	set -- "$COMMAND" $DEFAULT_ARGS "$@"
fi

exec "$@"
