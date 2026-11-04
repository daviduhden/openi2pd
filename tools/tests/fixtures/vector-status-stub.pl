#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: deterministic accepted-vector status fixture.
#
# Test-only executable for the unexpected-pass checker regression.
# Operands are intentionally ignored; no router or protocol runs here.

use 5.016;
use strict;
use warnings;

print '{"status":"accepted"}';
