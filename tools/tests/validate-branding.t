#!/usr/bin/env perl
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
#
# Test::More wrapper for the static branding validator. It runs from the
# repository root so the validator's relative asset paths are deterministic.

use strict;
use warnings;

use FindBin qw($Bin);
use File::Spec;
use Test::More;

my $script = File::Spec->catfile($Bin, File::Spec->updir, 'scripts', 'validate-branding.pl');
ok(-f $script, 'validate-branding.pl exists');

ok(open(my $fh, '-|', $^X, $script), 'validator starts');
my $output = '';
if ($fh) {
    local $/;
    $output = <$fh>;
    close $fh;
}
is($?, 0, 'validator exits successfully');
like($output, qr/branding validation passed/, 'validator reports success');

done_testing();
