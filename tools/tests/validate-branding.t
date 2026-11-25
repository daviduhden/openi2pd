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
use 5.016;

use FindBin qw($Bin);
use File::Spec;
use Test::More;

my $script = File::Spec->catfile( $Bin, File::Spec->updir, 'scripts',
    'validate-branding.pl' );
ok( -f $script, 'validate-branding.pl exists' );

sub capture_validator {
    my ($path) = @_;
    open my $fh, '-|', $^X, $path or return ();
    local $/ = undef;
    my $output = <$fh> // '';
    my $closed = close $fh;
    return ( 1, $output, $closed, $? );
}

my ( $opened, $output, $closed, $status ) = capture_validator($script);
ok( $opened, 'validator starts' );
if ( !$opened ) {
    fail('validator exits successfully');
    fail('validator reports success');
}
else {
    # A signal can leave the shifted exit value at zero. Require the
    # successful pipe close and the complete wait word, not just >> 8.
    ok( $closed && $status == 0, 'validator exits successfully' );
    like( $output, qr/branding validation passed/,
        'validator reports success' );
}

done_testing();
