#!/usr/bin/env perl
# Copyright (c) 2013-2026, The PurpleI2P Project
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
# Part of openi2pd; see LICENSE.
use strict;
use warnings;
use File::Spec;
use Cwd qw(abs_path);
use Digest::SHA qw(sha256_hex);

my $root = abs_path(File::Spec->curdir());
my @required = qw(
    assets/branding/logo.svg
    assets/branding/logo.png
    assets/branding/symbol.svg
    assets/branding/symbol.png
    assets/branding/logo-horizontal.svg
    assets/branding/logo-monochrome.svg
    assets/branding/logo-light.svg
    assets/branding/logo-dark.svg
    assets/branding/logo-original.png
    assets/branding/colors.md
    contrib/artwork/open-i2pd-icon.svg
    docs/branding/README.md
    docs/branding/colors.md
    docs/branding/logo-usage.md
    docs/branding/assets.md
);

my $failed = 0;
for my $rel (@required) {
    my $path = File::Spec->catfile($root, split m{/}, $rel);
    if (!-f $path) {
        print STDERR "missing: $rel\n";
        $failed++;
    }
}

my %svg = map { $_ => 1 } qw(
    logo.svg symbol.svg logo-horizontal.svg logo-monochrome.svg
    logo-light.svg logo-dark.svg
);
my @svgs = ((map { "assets/branding/$_" } sort keys %svg), 'contrib/artwork/open-i2pd-icon.svg');
for my $rel (@svgs) {
    my $path = File::Spec->catfile($root, split m{/}, $rel);
    next unless -f $path;
    my $name = $rel;
    open my $fh, '<', $path or do { print STDERR "open $name: $!\n"; $failed++; next };
    local $/;
    my $text = <$fh>;
    close $fh;
    for my $bad ('<image', 'data:image', '@font-face', '<text', 'font-family', '<script', 'xlink:href="http') {
        if (index($text, $bad) >= 0) {
            print STDERR "$name contains forbidden $bad\n";
            $failed++;
        }
    }
    if ($text !~ /<svg\b/ || $text !~ /<path\b/ || $text !~ /viewBox=/) {
        print STDERR "$name is not an SVG path document with a viewBox\n";
        $failed++;
    }
}

sub png_size {
    my ($path) = @_;
    open my $fh, '<:raw', $path or return;
    read($fh, my $sig, 8);
    return unless defined $sig && $sig eq "\x89PNG\r\n\x1a\n";
    read($fh, my $ihdr, 8);
    return unless length($ihdr) == 8 && substr($ihdr, 4, 4) eq 'IHDR';
    read($fh, my $data, 8);
    close $fh;
    return unless length($data) == 8;
    my ($w, $h) = unpack('NN', $data);
    return ($w, $h);
}

for my $pair (
    ['assets/branding/logo.png', 1254, 1254],
    ['assets/branding/symbol.png', 1024, 1024],
    ['assets/branding/logo-original.png', 1254, 1254],
) {
    my ($rel, $w, $h) = @$pair;
    my $path = File::Spec->catfile($root, split m{/}, $rel);
    next unless -f $path;
    my ($pw, $ph) = png_size($path);
    if (!defined $pw || $pw != $w || $ph != $h) {
        print STDERR "$rel has unexpected dimensions\n";
        $failed++;
    }
}

my $original = File::Spec->catfile($root, 'assets', 'branding', 'logo-original.png');
if (-f $original) {
    my $expected = '9e8dc2d7a8e330112f28011ebfe993871a01e9cd8be70c8cb85cb29c0d76112f';
    my $actual = sha256_hex(do { open my $fh, '<:raw', $original or die $!; local $/; <$fh> });
    if ($actual ne $expected) {
        print STDERR "logo-original.png SHA-256 changed\n";
        $failed++;
    }
}

if ($failed) {
    print STDERR "branding validation failed ($failed issue(s))\n";
    exit 1;
}
print "branding validation passed\n";
exit 0;
