#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: validate branding.
#
# Test::More wrapper that runs the static branding validator from the
# repository root and checks its success output. It performs no builds or
# network requests.
#

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
