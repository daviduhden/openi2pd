#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check i18n catalogs.
#
# Compile-free contract for the native Spanish, Russian and Simplified Chinese
# catalogs. It verifies key parity, placeholder and trailing-space parity,
# UTF-8/locale hygiene, plural-form arity, that every translatable source
# literal has an entry, and that protocol/configuration identifiers are never
# translated. It never compiles, runs a binary or writes a file.
#
# usage: check-i18n-catalogs.pl [--root DIR]

use strict;
use warnings;
use 5.016;
use Getopt::Long qw(GetOptions);
use File::Spec;
use File::Basename qw(dirname);
use Cwd            ();

my $root = Cwd::abs_path(
    File::Spec->catdir( dirname( File::Spec->rel2abs($0) ), '..' ) );
GetOptions( 'root=s' => \$root ) or usage(2);
$root = Cwd::abs_path($root);
usage(2) unless defined $root && -d $root;

my @catalogs = (
    {
        name => 'es',
        path => File::Spec->catfile( $root, 'i18n', 'Spanish.cpp' )
    },
    {
        name => 'ru',
        path => File::Spec->catfile( $root, 'i18n', 'Russian.cpp' )
    },
    {
        name => 'zh',
        path => File::Spec->catfile( $root, 'i18n', 'ChineseSimplified.cpp' )
    },
);
my @sources = map { File::Spec->catfile( $root, split m{/}, $_ ) } (
    'tui/main.cpp',      'libi2pd_client/HTTPProxy.cpp',
    'daemon/Daemon.cpp', 'tools/autoconf_i2pd.cpp'
);

my @errors;
sub fail { push @errors, join( '', @_ ) . "\n" }

sub usage {
    my ($code) = @_;
    print STDERR "usage: $0 [--root DIR]\n";
    exit $code;
}

sub read_file {
    my ($path) = @_;
    open my $fh, '<:raw', $path or die "open $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    return $text // '';
}

sub unescape {
    my ($value) = @_;
    $value =~ s/\\n/\n/g;
    $value =~ s/\\t/\t/g;
    $value =~ s/\\"/"/g;
    $value =~ s/\\\\/\\/g;
    return $value;
}

sub parse_strings {
    my ( $text, $path ) = @_;
    my $start = index( $text, 'static const LocaleStrings strings {' );
    fail("$path has no LocaleStrings table") if $start < 0;
    my $end = index( $text, 'static const std::map', $start );
    $end = length($text) if $end < 0;
    my $block = substr( $text, $start, $end - $start );
    my %strings;
    while (
        $block =~ /\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}/g )
    {
        my ( $key, $value ) = ( unescape($1), unescape($2) );
        fail("duplicate key in $path: $key") if exists $strings{$key};
        $strings{$key} = $value;
    }
    return \%strings;
}

sub parse_plurals {
    my ($text) = @_;
    my $start = index( $text,
        'static const std::map<std::string, std::vector<std::string>> plurals'
    );
    return {} if $start < 0;
    my $block = substr( $text, $start );
    my $end   = index( $block, '};' );
    $block = substr( $block, 0, $end + 2 ) if $end >= 0;
    my %plurals;
    while ( $block =~ /\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*\{([^}]*)\}\s*\}/g ) {
        my $key = unescape($1);
        my @forms;
        my $list = $2;
        while ( $list =~ /"((?:[^"\\]|\\.)*)"/g ) {
            push @forms, unescape($1);
        }
        $plurals{$key} = \@forms;
    }
    return \%plurals;
}

sub placeholders {
    my ($value) = @_;
    my @found;
    while ( $value =~ /%(?!%)(?:\d+\$)?[-+ 0#]*\d*(?:\.\d+)?([sdiuf])/g ) {
        push @found, $1;
    }
    return join( ',', sort @found );
}

sub valid_utf8 {
    my ($value) = @_;
    return $value !~ /[\x80-\xFF]/ || $value =~ /^(?:
        [\x00-\x7F]|
        [\xC2-\xDF][\x80-\xBF]|
        \xE0[\xA0-\xBF][\x80-\xBF]|
        [\xE1-\xEC\xEE\xEF][\x80-\xBF]{2}|
        \xED[\x80-\x9F][\x80-\xBF]|
        \xF0[\x90-\xBF][\x80-\xBF]{2}|
        [\xF1-\xF3][\x80-\xBF]{3}|
        \xF4[\x80-\x8F][\x80-\xBF]{2}
    )*$/sx;
}

my %catalog;
for my $catalog (@catalogs) {
    fail("missing catalog $catalog->{path}") unless -f $catalog->{path};
    next                                     unless -f $catalog->{path};
    my $text = read_file( $catalog->{path} );
    fail("BOM in $catalog->{path}")  if $text =~ /^\xEF\xBB\xBF/;
    fail("CRLF in $catalog->{path}") if $text =~ /\r\n/;
    fail("invalid UTF-8 in $catalog->{path}") unless valid_utf8($text);
    my $strings = parse_strings( $text, $catalog->{path} );
    my $plurals = parse_plurals($text);
    $catalog->{strings} = $strings;
    $catalog->{plurals} = $plurals;

    for my $key ( keys %$strings ) {
        my $value = $strings->{$key};
        fail("invalid UTF-8 value for '$key' in $catalog->{path}")
          unless valid_utf8($value);
        my $want = placeholders($key);
        my $got  = placeholders($value);
        fail(
"placeholder mismatch for '$key' in $catalog->{path} ($want != $got)"
        ) if $want ne $got;
        if ( $key =~ / $/ && $value =~ /[A-Za-z0-9:;,.!?)]$/ ) {
            fail("trailing space lost for '$key' in $catalog->{path}");
        }
    }

    # A protocol/config identifier must never become a catalog key.
    for my $key ( keys %$strings ) {
        fail("translated identifier '$key' in $catalog->{path}")
          if $key =~
/^(?:ntcp2|ssu2|httpproxy|i2cp|torrent|privops|privsep|openbsd|log|http)\./
          || $key =~ /^(?:TORRENT-|ADMIN |I2PDTUI\/)/
          || $key =~ /^magnet:\?/;
    }
}

# Key parity between the three catalogs.
my ($first) = @catalogs;
for my $catalog ( @catalogs[ 1 .. $#catalogs ] ) {
    my $a = $catalog->{strings};
    my $b = $first->{strings};
    for my $key ( sort keys %$b ) {
        fail("$catalog->{name} is missing '$key'") unless exists $a->{$key};
    }
    for my $key ( sort keys %$a ) {
        fail("$first->{name} is missing '$key'") unless exists $b->{$key};
    }
}

# Plural arity: Spanish two forms, Russian three, Simplified Chinese one.
my %expected = ( es => 2, ru => 3, zh => 1 );
for my $catalog (@catalogs) {
    my $plurals = $catalog->{plurals};
    my $want    = $expected{ $catalog->{name} };
    for my $key ( sort keys %$plurals ) {
        my $forms = $plurals->{$key};
        fail(   "plural '$key' in $catalog->{name} has "
              . scalar(@$forms)
              . " forms, expected $want" )
          if scalar(@$forms) != $want;
        for my $form (@$forms) {
            fail("empty plural form '$key' in $catalog->{name}") if $form eq '';
        }
    }
}
my $basePlurals = $first->{plurals};
for my $catalog ( @catalogs[ 1 .. $#catalogs ] ) {
    my $a = $catalog->{plurals};
    for my $key ( sort keys %$basePlurals ) {
        fail("plural '$key' missing in $catalog->{name}")
          unless exists $a->{$key};
    }
    for my $key ( sort keys %$a ) {
        fail("plural '$key' missing in $first->{name}")
          unless exists $basePlurals->{$key};
    }
}

# Every translatable source literal must exist in all three catalogs.
sub harvest {
    my ($path) = @_;
    my $text = read_file($path);
    my %keys;

    # Adjacent string literals are concatenated by the C++ compiler before the
    # lookup, so the catalog key is the whole argument, not its first fragment.
    my $literal_run = qr/((?:"(?:[^"\\]|\\.)*"\s*)+)/;
    while ( $text =~ /\btr\s*\(\s*$literal_run/g ) {
        my @lits = $1 =~ /"((?:[^"\\]|\\.)*)"/g;
        $keys{ unescape( join( '', @lits ) ) } = 1;
    }
    while ( $text =~ /\bSetStatus\s*\(\s*$literal_run/g ) {
        my @lits = $1 =~ /"((?:[^"\\]|\\.)*)"/g;
        $keys{ unescape( join( '', @lits ) ) } = 1;
    }
    while ( $text =~ /\bBeginPrompt\s*\([^,]+,\s*$literal_run/g ) {
        my @lits = $1 =~ /"((?:[^"\\]|\\.)*)"/g;
        $keys{ unescape( join( '', @lits ) ) } = 1;
    }

    # Label helpers in the TUI translate internally; harvest literal labels so
    # a missing catalog entry cannot silently fall back to English.
    if ( $path =~ m{/tui/main\.cpp$} ) {
        while ( $text =~ /\b(?:show|add)\s*\(\s*$literal_run/g ) {
            my @lits = $1 =~ /"((?:[^"\\]|\\.)*)"/g;
            $keys{ unescape( join( '', @lits ) ) } = 1;
        }
    }
    return \%keys;
}

my %used;
for my $source (@sources) {
    fail("missing source $source") unless -f $source;
    next                           unless -f $source;
    my $keys = harvest($source);
    $used{$_} = $source for keys %$keys;
}
for my $key ( sort keys %used ) {
    for my $catalog (@catalogs) {
        fail(
"$catalog->{name} catalog is missing source literal '$key' ($used{$key})"
        ) unless exists $catalog->{strings}{$key};
    }
}

if (@errors) {
    print STDERR @errors;
    exit 1;
}
my $count       = scalar keys %{ $catalogs[0]{strings} };
my $pluralCount = scalar keys %{ $catalogs[0]{plurals} };
print
"i18n catalogs: $count keys, $pluralCount plural groups, en/es/ru/zh-Hans parity OK\n";
exit 0;
