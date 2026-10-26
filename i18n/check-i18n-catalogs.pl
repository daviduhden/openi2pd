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
    # The singular table ends where the plural map begins. Parsing stays
    # textual on purpose: the checker must run without a C++ compiler.
    my $end = index( $text, 'static const std::map', $start );
    $end = length($text) if $end < 0;
    my $block = substr( $text, $start, $end - $start );
    my %strings;
    my $literal_run = qr/((?:"(?:[^"\\]|\\.)*"\s*)+)/;
    while (
        $block =~ /\{\s*$literal_run\s*,\s*$literal_run\s*\}/g )
    {
        # A nested match below resets $1/$2, so snapshot both literal runs
        # before extracting the individual string fragments from them.
        my ( $key_run, $value_run ) = ( $1, $2 );
        my @key_parts = $key_run =~ /"((?:[^"\\]|\\.)*)"/g;
        my @value_parts = $value_run =~ /"((?:[^"\\]|\\.)*)"/g;
        my ( $key, $value ) = (
            unescape( join( '', @key_parts ) ),
            unescape( join( '', @value_parts ) )
        );
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
    # Conversions are compared as a sorted multiset, never by position: a
    # translation may reorder a sentence, but the set of %s/%d/%i/%u/%f
    # conversions (with widths, flags and positional indexes ignored) must stay
    # identical. "%%" is a literal percent sign and never counts.
    my @found;
    while ( $value =~ /%(?!%)(?:\d+\$)?[-+ 0#]*\d*(?:\.\d+)?([sdiuf])/g ) {
        push @found, $1;
    }
    return join( ',', sort @found );
}

sub valid_utf8 {
    my ($value) = @_;
    # Reject overlong encodings, UTF-16 surrogates and code points above
    # U+10FFFF. The catalogs are compiled as UTF-8 literals and the TUI
    # measures display columns, so an invalid sequence would corrupt output
    # long before any build step noticed it.
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
        # A trailing space in the English key is a word separator in a composed
        # TUI line. If a translation stores its last word without it, the next
        # label runs into the sentence, so treat a lost space as a defect.
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

=head1 NAME

check-i18n-catalogs.pl - verify the native translation catalogs

=head1 SYNOPSIS

    perl i18n/check-i18n-catalogs.pl [--root DIR]

DIR defaults to the repository containing the script.

=head1 DESCRIPTION

Compile-free contract for the Spanish, Russian and Simplified Chinese
catalogs in F<i18n/>. The English source strings are the keys, so this checker
compares each translation against the source literals rather than against a
generated message file. It fails when:

=over 4

=item *

a catalog is missing, contains a byte-order mark or CRLF, or is not valid
UTF-8;

=item *

a key appears twice, or the three catalogs do not have exactly the same key
set;

=item *

a printf conversion set differs from the English key, or a trailing separator
space was dropped;

=item *

a plural group has the wrong arity for its language (two forms for Spanish,
three for Russian, one for Simplified Chinese), an empty form, or a key set
that differs from the other catalogs;

=item *

a key is a protocol, configuration or wire identifier (for example an
C<ntcp2.*> or C<torrent.*> key, a C<TORRENT-> verb, an C<< ADMIN >> verb, an
C<I2PDTUI/> request or a magnet link), because those must never be
translated; or

=item *

a translatable literal harvested from F<tui/main.cpp>,
F<libi2pd_client/HTTPProxy.cpp>, F<daemon/Daemon.cpp> or
F<tools/autoconf_i2pd.cpp> has no entry in one of the catalogs.

=back

=head1 FILE FORMAT

The catalogs are C++ sources holding a C<LocaleStrings> initialiser and a
plural map. Entries are read textually: adjacent string literals are
concatenated before comparison, because the compiler joins them into one key
and a translation stored against a single fragment would never match.
Harvested call sites are C<tr(...)>, C<SetStatus(...)>,
C<BeginPrompt(...)> and, in F<tui/main.cpp> only, the label helpers
C<show(...)> and C<add(...)>.

=head1 OUTPUT AND EXIT STATUS

Every violation is written to STDERR and the script exits 1. A clean run
prints one summary line on STDOUT and exits 0. Invalid arguments or a missing
root print the usage line and exit 2.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, Getopt::Long, File::Spec, File::Basename,
Cwd, 5.016). No compiler, catalog generator or locale installation is
required, which is why the check runs on OpenBSD base Perl.

=head1 SECURITY AND PORTABILITY

Read-only: no file is written, no program is compiled and no network is used.
Diagnostics quote catalog keys but never a credential; catalogs contain UI
text only.
