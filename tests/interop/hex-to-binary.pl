#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
# Emit validated hexadecimal corpus bytes without external converters.
use 5.016;
use strict;
use warnings;

@ARGV == 1 or die "usage: hex-to-binary.pl HEX_FILE\n";
open my $input, '<:raw', $ARGV[0] or die "cannot open hex input\n";
my $hex = '';
while (1) {
    my $count = read $input, my $chunk, 8192;
    defined $count or die "cannot read hex input\n";
    last unless $count;
    $hex .= $chunk;
    length($hex) <= 16 * 1024 * 1024 or die "hex input exceeds limit\n";
}
close $input or die "cannot close hex input\n";
$hex =~ s/ ^ [ \t]* [#] .* $ //mgx;
$hex =~ s/ \s+ //gx;
die "invalid hex input\n" if length($hex) % 2 || $hex =~ / [^0-9a-fA-F] /x;
binmode STDOUT;
print STDOUT pack( 'H*', $hex ) or die "cannot write corpus bytes\n";
close STDOUT                    or die "cannot close corpus output\n";
