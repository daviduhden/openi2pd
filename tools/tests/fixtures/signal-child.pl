#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: POSIX signal-exit status fixture.
#
# Test-only child used to distinguish signal death from a normal exit.

use 5.016;
use strict;
use warnings;

kill 'TERM', $$;
