#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: validate branding.
#
# Validates the branding asset inventory, SVG vector constraints, PNG
# dimensions, palette definitions and documentation mirror hashes. It only
# reads files and exits non-zero for every missing or non-conforming asset.
#

use strict;
use warnings;
use File::Spec;
use Cwd         qw(abs_path);
use Digest::SHA qw(sha256_hex);
use FindBin     qw($Bin);

my $default_root =
  File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir );
my $root = abs_path( defined $ARGV[0] ? $ARGV[0] : $default_root );
die "branding validation: cannot resolve repository root\n"
  unless defined $root;
die "branding validation: $root is not the repository root\n"
  unless -d File::Spec->catdir( $root, 'assets', 'branding' );
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
  contrib/artwork/openi2pd-256.png
  docs/img/logo.png
  docs/img/favicon.ico
  Win32/openi2pd.ico
  android/app/src/main/res/mipmap-anydpi/ic_launcher.xml
  android/app/src/main/res/mipmap-anydpi-v33/ic_launcher.xml
  docs/branding/README.md
  docs/branding/colors.md
  docs/branding/logo-usage.md
  docs/branding/assets.md
  docs/assets/branding/colors.md
  docs/assets/branding/logo.svg
  docs/assets/branding/symbol.svg
  docs/assets/branding/logo-horizontal.svg
  docs/assets/branding/logo-monochrome.svg
  docs/assets/branding/logo-light.svg
  docs/assets/branding/logo-dark.svg
  docs/assets/branding/logo.png
  docs/assets/branding/symbol.png
  docs/assets/branding/logo-original.png
);

my $failed = 0;
for my $rel (@required) {
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    if ( !-f $path ) {
        print STDERR "missing: $rel\n";
        $failed++;
    }
}

my %svg = map { $_ => 1 } qw(
  logo.svg symbol.svg logo-horizontal.svg logo-monochrome.svg
  logo-light.svg logo-dark.svg
);
my @svgs = (
    ( map { "assets/branding/$_" } sort keys %svg ),
    'contrib/artwork/open-i2pd-icon.svg'
);
for my $rel (@svgs) {
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    next unless -f $path;
    my $name = $rel;
    open my $fh, '<', $path
      or do { print STDERR "open $name: $!\n"; $failed++; next };
    local $/;
    my $text = <$fh>;
    close $fh;
    for my $bad (
        '<image',      'data:image', '@font-face', '<text',
        'font-family', '<script',    'xlink:href="http'
      )
    {
        if ( index( $text, $bad ) >= 0 ) {
            print STDERR "$name contains forbidden $bad\n";
            $failed++;
        }
    }
    if ( $text !~ /<svg\b/ || $text !~ /<path\b/ || $text !~ /viewBox=/ ) {
        print STDERR "$name is not an SVG path document with a viewBox\n";
        $failed++;
    }
}

sub png_size {
    my ($path) = @_;
    open my $fh, '<:raw', $path or return;
    read( $fh, my $sig, 8 );
    return unless defined $sig && $sig eq "\x89PNG\r\n\x1a\n";
    read( $fh, my $ihdr, 8 );
    return unless length($ihdr) == 8 && substr( $ihdr, 4, 4 ) eq 'IHDR';
    read( $fh, my $data, 8 );
    close $fh;
    return unless length($data) == 8;
    my ( $w, $h ) = unpack( 'NN', $data );
    return ( $w, $h );
}

for my $pair (
    [ 'assets/branding/logo.png',          1254, 1254 ],
    [ 'assets/branding/symbol.png',        1024, 1024 ],
    [ 'assets/branding/logo-original.png', 1254, 1254 ],
    [ 'contrib/artwork/openi2pd-256.png',  256,  256 ],
    [ 'docs/img/logo.png',                 512,  512 ],
  )
{
    my ( $rel, $w, $h ) = @$pair;
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    next unless -f $path;
    my ( $pw, $ph ) = png_size($path);
    if ( !defined $pw || $pw != $w || $ph != $h ) {
        print STDERR "$rel has unexpected dimensions\n";
        $failed++;
    }
}

sub ico_sizes {
    my ($path) = @_;
    open my $fh, '<:raw', $path or return;
    read( $fh, my $header, 6 );
    return unless defined $header && length($header) == 6;
    my ( $reserved, $type, $count ) = unpack( 'vvv', $header );
    return unless $reserved == 0 && $type == 1;
    my @sizes;
    for ( 1 .. $count ) {
        read( $fh, my $entry, 16 );
        return unless defined $entry && length($entry) == 16;
        my ( $width, $height ) = unpack( 'CC', $entry );
        $width  = 256 if $width == 0;
        $height = 256 if $height == 0;
        push @sizes, $width if $width == $height;
    }
    close $fh;
    return @sizes;
}

for my $pair (
    [ 'docs/img/favicon.ico', [ 16, 32, 48, 64 ] ],
    [ 'Win32/openi2pd.ico',   [ 16, 24, 32, 48, 64, 128, 256 ] ],
  )
{
    my ( $rel, $expected ) = @$pair;
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    next unless -f $path;
    my @actual = sort { $a <=> $b } ico_sizes($path);
    my @wanted = sort { $a <=> $b } @$expected;
    if ( join( ',', @actual ) ne join( ',', @wanted ) ) {
        print STDERR "$rel has unexpected ICO sizes ("
          . join( ',', @actual ) . ")\n";
        $failed++;
    }
}

my $symbol_path =
  File::Spec->catfile( $root, 'assets', 'branding', 'symbol.svg' );
my $artwork_path =
  File::Spec->catfile( $root, 'contrib', 'artwork', 'open-i2pd-icon.svg' );
if ( -f $symbol_path && -f $artwork_path ) {
    my $symbol_hash = sha256_hex(
        do { open my $fh, '<:raw', $symbol_path or die $!; local $/; <$fh> }
    );
    my $artwork_hash = sha256_hex(
        do { open my $fh, '<:raw', $artwork_path or die $!; local $/; <$fh> }
    );
    if ( $symbol_hash ne $artwork_hash ) {
        print STDERR
"contrib/artwork/open-i2pd-icon.svg differs from assets/branding/symbol.svg\n";
        $failed++;
    }
}

my $original =
  File::Spec->catfile( $root, 'assets', 'branding', 'logo-original.png' );
if ( -f $original ) {
    my $expected =
      '9e8dc2d7a8e330112f28011ebfe993871a01e9cd8be70c8cb85cb29c0d76112f';
    my $actual = sha256_hex(
        do { open my $fh, '<:raw', $original or die $!; local $/; <$fh> }
    );
    if ( $actual ne $expected ) {
        print STDERR "logo-original.png SHA-256 changed\n";
        $failed++;
    }
}

# Palette centralization: every hex value in a platform file must appear in
# assets/branding/colors.md. This catches an Android/TUI/CSS palette drifting
# away from the canonical source without requiring a build.
my %canonical;
my $colors_rel  = 'assets/branding/colors.md';
my $colors_path = File::Spec->catfile( $root, split m{/}, $colors_rel );
if ( open my $colors_fh, '<', $colors_path ) {
    while ( my $line = <$colors_fh> ) {
        if ( $line =~ /^\|\s*`[^`]+`\s*\|\s*`(#[0-9A-Fa-f]{6})`\s*\|/ ) {
            $canonical{ uc $1 } = 1;
        }
    }
    close $colors_fh;
}
else {
    print STDERR "cannot read $colors_rel\n";
    $failed++;
}

sub check_palette_file {
    my ( $rel, $pattern ) = @_;
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    if ( !-f $path ) {
        print STDERR "palette file missing: $rel\n";
        $failed++;
        return 0;
    }
    open my $fh, '<', $path
      or do { print STDERR "open $rel: $!\n"; $failed++; return 0 };
    local $/;
    my $text = <$fh>;
    close $fh;
    my $count = 0;
    while ( $text =~ /$pattern/g ) {
        my $hex = uc $1;
        $hex = "#$hex" unless $hex =~ /^#/;
        $count++;
        if ( !$canonical{$hex} ) {
            print STDERR "$rel contains non-canonical colour $hex\n";
            $failed++;
        }
    }
    if ( $count == 0 ) {
        print STDERR "$rel contains no palette values\n";
        $failed++;
    }
    return $count;
}

for my $core ( '#00B2B4', '#078DE4', '#162737' ) {
    if ( !$canonical{$core} ) {
        print STDERR "canonical palette is missing $core\n";
        $failed++;
    }
}

check_palette_file(
    'android/app/src/main/res/values/colors.xml',
    qr/\bcolor\b[^>]*>\s*(#[0-9A-Fa-f]{6})\s*</
);
check_palette_file(
    'android/app/src/main/res/values-night/colors.xml',
    qr/\bcolor\b[^>]*>\s*(#[0-9A-Fa-f]{6})\s*</
);
check_palette_file( 'docs/stylesheets/brand.css',
    qr/--oi-[a-z-]+\s*:\s*(#[0-9A-Fa-f]{6})/ );
check_palette_file( 'tui/Theme.h', qr/FromHex\s*\(\s*0x([0-9A-Fa-f]{6})\s*\)/ );
check_palette_file( 'libi2pd_client/HTTPProxy.cpp',
    qr/(?:color|background)\s*:\s*(#[0-9A-Fa-f]{6})/ );

sub normalize_hex {
    my ($hex) = @_;
    $hex = uc $hex;
    $hex =~ s/^#//;
    if ( length($hex) == 8 && substr( $hex, 0, 2 ) eq 'FF' ) {
        $hex = substr( $hex, 2 );
    }
    return "#$hex";
}

# Brand SVG and Android vector fills must also use the canonical palette. Do
# not apply this to generic UI action icons: they use #FF000000 plus a theme
# tint, not a brand colour.
sub check_svg_palette {
    my ($rel) = @_;
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    return unless -f $path;
    open my $fh, '<', $path
      or do { print STDERR "open $rel: $!\n"; $failed++; return };
    local $/;
    my $text = <$fh>;
    close $fh;
    while ( $text =~ /fill="(#[0-9A-Fa-f]{6})"/g ) {
        my $hex = normalize_hex($1);
        if ( !$canonical{$hex} ) {
            print STDERR "$rel contains non-canonical SVG fill $hex\n";
            $failed++;
        }
    }
}

for my $rel (
    qw(
    assets/branding/logo.svg
    assets/branding/symbol.svg
    assets/branding/logo-horizontal.svg
    assets/branding/logo-monochrome.svg
    assets/branding/logo-light.svg
    assets/branding/logo-dark.svg
    contrib/artwork/open-i2pd-icon.svg
    )
  )
{
    check_svg_palette($rel);
}

sub check_android_vector_palette {
    my ($rel) = @_;
    my $path = File::Spec->catfile( $root, split m{/}, $rel );
    return unless -f $path;
    open my $fh, '<', $path
      or do { print STDERR "open $rel: $!\n"; $failed++; return };
    local $/;
    my $text = <$fh>;
    close $fh;
    while ( $text =~ /android:fillColor="(#[0-9A-Fa-f]{6,8})"/g ) {
        my $hex = normalize_hex($1);
        if ( !$canonical{$hex} ) {
            print STDERR "$rel contains non-canonical vector fill $hex\n";
            $failed++;
        }
    }
}

for my $rel (
    qw(
    android/app/src/main/res/drawable/ic_logo.xml
    android/app/src/main/res/drawable/ic_launcher_foreground.xml
    android/app/src/main/res/drawable/ic_launcher_monochrome.xml
    android/app/src/main/res/drawable/ic_logo_monochrome.xml
    )
  )
{
    check_android_vector_palette($rel);
}

# The MkDocs mirror must not silently drift from the canonical SVG masters.
for my $name (
    qw(logo.svg symbol.svg logo-horizontal.svg logo-monochrome.svg logo-light.svg logo-dark.svg
    logo.png symbol.png logo-original.png)
  )
{
    my $canonical = File::Spec->catfile( $root, 'assets', 'branding', $name );
    my $mirror =
      File::Spec->catfile( $root, 'docs', 'assets', 'branding', $name );
    if ( !-f $canonical || !-f $mirror ) {
        print STDERR "docs/assets/branding/$name is missing\n";
        $failed++;
        next;
    }
    my $canonical_hash = sha256_hex(
        do { open my $fh, '<:raw', $canonical or die $!; local $/; <$fh> }
    );
    my $mirror_hash = sha256_hex(
        do { open my $fh, '<:raw', $mirror or die $!; local $/; <$fh> }
    );
    if ( $canonical_hash ne $mirror_hash ) {
        print STDERR
          "docs/assets/branding/$name differs from assets/branding/$name\n";
        $failed++;
    }
}

my $docs_colors =
  File::Spec->catfile( $root, 'docs', 'assets', 'branding', 'colors.md' );
if ( -f $docs_colors ) {
    my $canonical_hash = sha256_hex(
        do { open my $fh, '<:raw', $colors_path or die $!; local $/; <$fh> }
    );
    my $docs_hash = sha256_hex(
        do { open my $fh, '<:raw', $docs_colors or die $!; local $/; <$fh> }
    );
    if ( $canonical_hash ne $docs_hash ) {
        print STDERR
          "docs/assets/branding/colors.md differs from $colors_rel\n";
        $failed++;
    }
}

if ($failed) {
    print STDERR "branding validation failed ($failed issue(s))\n";
    exit 1;
}
print
"branding validation passed (palette matches $colors_rel; docs copy is identical)\n";
exit 0;
