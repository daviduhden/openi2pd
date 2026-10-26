#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check android locales.
#
# Compile-free locale contract check. Verifies the supported resource
# directories, locales_config, Gradle filters, Fastlane allowlist, catalog key
# parity, positional placeholder parity, non-translatable identifier handling
# and the per-app locale override boundary. It never runs Gradle or a device.
#

use strict;
use warnings;
use 5.016;
use File::Find qw(find);
use File::Spec;

my @languages    = qw(en es ru zh-CN);
my %language_dir = (
    'en'    => 'values',
    'es'    => 'values-es',
    'ru'    => 'values-ru',
    'zh-CN' => 'values-zh-rCN'
);
my %allowed_store = map { $_ => 1 } qw(en-US es-ES ru-RU zh-CN);
# The checker is run from the repository root by CI and from android/ by hand.
# Probe the known layout instead of trusting the current directory.
my $root          = -d 'android/app/src/main' ? 'android' : '.';
my $res           = File::Spec->catdir( $root, qw(app src main res) );
my @errors;

sub fail { push @errors, join( '', @_ ) . "\n" }

sub read_file {
    my ($path) = @_;
    open( my $fh, '<:raw', $path ) or die "open $path: $!";
    local $/;
    my $text = <$fh>;
    close($fh);
    # Encoding defects are recorded instead of aborting so one run reports
    # every affected file rather than only the first.
    fail("BOM in $path")  if $text =~ /^\xEF\xBB\xBF/;
    fail("CRLF in $path") if $text =~ /\r\n/;
    return $text;
}

sub catalog {
    my ($path) = @_;
    my $xml = read_file($path);
    my ( %strings, %attrs );
    while ( $xml =~ /<string\s+([^>]*?)>(.*?)<\/string>/sg ) {
        my ( $raw_attrs, $value ) = ( $1, $2 );
        next unless $raw_attrs =~ /\bname="([^"]+)"/;
        my $name = $1;
        fail("duplicate string $name in $path") if exists $strings{$name};
        $strings{$name} = $value;
        $attrs{$name}   = $raw_attrs;
    }
    return ( \%strings, \%attrs );
}

sub plurals {
    my ($path) = @_;
    my $xml = read_file($path);
    my %out;
    while ( $xml =~ /<plurals\s+name="([^"]+)"[^>]*>(.*?)<\/plurals>/sg ) {
        my ( $name, $body ) = ( $1, $2 );
        my %quantity;
        while ( $body =~ /<item\s+quantity="([^"]+)"[^>]*>(.*?)<\/item>/sg ) {
            $quantity{$1} = $2;
        }
        $out{$name} = \%quantity;
    }
    return \%out;
}

sub placeholders {
    my ($value) = @_;
    my @matches = ( $value =~ /%(?:\d+\$)?[-+ 0#]*\d*(?:\.\d+)?[sd]/g );
    return join( ',', sort @matches );
}

if ( !-d $res ) {
    print "FAIL: Android resource directory not found\n";
    exit 1;
}
opendir( my $dh, $res ) or die "opendir($res): $!";
my @value_dirs = grep { /^values(?:-|$)/ } readdir($dh);
closedir($dh);
for my $dir ( sort @value_dirs ) {
    next if $dir eq 'values';
    # Configuration qualifiers are not locales and are legitimate siblings.
    next if $dir =~ /^values-(?:night|sw\d+dp|w\d+dp|v\d+)$/;
    fail("unsupported locale resource directory: res/$dir")
      unless $dir eq 'values-es'
      || $dir eq 'values-ru'
      || $dir eq 'values-zh-rCN';
}

my $base_path = File::Spec->catfile( $res, qw(values strings.xml) );
my ( $base, $base_attrs ) = catalog($base_path);
my %translations;
for my $tag (qw(es ru zh-CN)) {
    my $path = File::Spec->catfile( $res, $language_dir{$tag}, 'strings.xml' );
    fail("missing locale catalog $path") unless -f $path;
    next                                 unless -f $path;
    my ( $translated, $attrs ) = catalog($path);
    $translations{$tag} = $translated;
    for my $name ( sort keys %$base ) {
        next if $base_attrs->{$name} =~ /translatable="false"/;
        fail("missing $tag string $name") unless exists $translated->{$name};
    }
    for my $name ( sort keys %$translated ) {
        fail("extra $tag string $name") unless exists $base->{$name};
        fail("translatable=false key $name present in $tag")
          if $base_attrs->{$name} =~ /translatable="false"/;
        next unless exists $base->{$name};
        my $want = placeholders( $base->{$name} );
        my $got  = placeholders( $translated->{$name} );
        fail("placeholder mismatch for $tag:$name ($want != $got)")
          if $want ne $got;
    }
}

# Plural resources must exist in every catalog with the quantities the
# language actually uses, and keep the same placeholders as the base form.
my $base_plurals  = plurals($base_path);
my %plural_counts = (
    es      => [qw(one other)],
    ru      => [qw(one few many other)],
    'zh-CN' => [qw(one other)]
);
for my $tag (qw(es ru zh-CN)) {
    my $path = File::Spec->catfile( $res, $language_dir{$tag}, 'strings.xml' );
    next unless -f $path;
    my $translated = plurals($path);
    for my $name ( sort keys %$base_plurals ) {
        fail("missing $tag plurals $name") unless exists $translated->{$name};
        next                               unless exists $translated->{$name};
        for my $quantity ( @{ $plural_counts{$tag} } ) {
            fail("missing $tag plurals $name quantity=$quantity")
              unless exists $translated->{$name}{$quantity};
        }
        my $want = placeholders( $base_plurals->{$name}{other} );
        for my $quantity ( keys %{ $translated->{$name} } ) {
            my $got = placeholders( $translated->{$name}{$quantity} );
            fail(
"placeholder mismatch for $tag plurals $name:$quantity ($want != $got)"
            ) if $want ne $got;
        }
    }
    for my $name ( sort keys %$translated ) {
        fail("extra $tag plurals $name") unless exists $base_plurals->{$name};
    }
}

# Android formatting is positional so a translated word order is safe. A bare
# %s/%d would bind to the first argument regardless of the sentence, so any
# non-positional conversion in the source catalog or a translation is a defect
# even when the current word order happens to work.
for my $name ( keys %$base ) {
    fail("non-positional placeholder in values string $name")
      if $base->{$name} =~ /%(?!%|\d+\$)[-+ 0#]*\d*(?:\.\d+)?[sdif]/;
}
for my $tag ( keys %translations ) {
    for my $name ( keys %{ $translations{$tag} } ) {
        fail("non-positional placeholder in $tag string $name")
          if $translations{$tag}{$name} =~
          /%(?!%|\d+\$)[-+ 0#]*\d*(?:\.\d+)?[sdif]/;
    }
}

if ( !-f File::Spec->catfile( $res, qw(xml locales_config.xml) ) ) {
    fail("missing res/xml/locales_config.xml");
}
else {
    my $xml =
      read_file( File::Spec->catfile( $res, qw(xml locales_config.xml) ) );
    my @locales = ( $xml =~ /<locale\s+android:name="([^"]+)"/g );
    fail("locales_config must declare en, es, ru, zh-CN")
      unless join( ',', @locales ) eq join( ',', @languages );
}

my $manifest = read_file(
    File::Spec->catfile( $root, qw(app src main AndroidManifest.xml) ) );
fail("AndroidManifest.xml does not declare localeConfig")
  unless $manifest =~ /android:localeConfig="\@xml\/locales_config"/;
fail("AndroidManifest.xml lacks AppCompat auto-store service")
  unless $manifest =~ /AppLocalesMetadataHolderService/;

my $gradle = read_file( File::Spec->catfile( $root, qw(app build.gradle) ) );
if ( $gradle =~ /androidResources\s*\{(.*?)\}/s ) {
    my $block   = $1;
    my @filters = ( $block =~ /localeFilters\s*\+=\s*\[([^\]]*)\]/s );
    my $joined  = join( '', @filters );
    my @tags    = ( $joined =~ /["']([^"']+)["']/g );
    fail("Gradle localeFilters must be en, es, ru, zh")
      unless join( ',', @tags ) eq 'en,es,ru,zh';
}
else {
    fail("Gradle androidResources block missing");
}

opendir( my $fdh, "$root/fastlane/metadata/android" )
  or die "open fastlane metadata: $!";
my @store_dirs =
  grep { /^[A-Za-z]/ && -d "$root/fastlane/metadata/android/$_" } readdir($fdh);
closedir($fdh);
for my $dir (@store_dirs) {
    fail("unsupported Fastlane locale $dir") unless $allowed_store{$dir};
}

my $prefs = read_file(
    File::Spec->catfile( $res, qw(values preferences_i2pd_options.xml) ) );
for my $array (
    qw(pref_language_values pref_bandwidth_values pref_loglevel_values))
{
    fail("$array must be translatable=false")
      unless $prefs =~
      /<string-array\s+name="$array"[^>]*translatable="false"/s;
}

# A process-wide locale override would change every activity and defeat the
# per-app AppCompat selection, so it is rejected in production sources.
# Whole-line comments are skipped and trailing // comments stripped so a
# documented example cannot fail the check; text inside a block comment that
# also carries code is still inspected.
find(
    {
        no_chdir => 1,
        wanted   => sub {
            return unless /\.(?:java|cpp|h)$/;
            open( my $fh, '<', $_ ) or return;
            my $line = 0;
            while ( my $text = <$fh> ) {
                ++$line;
                next if $text =~ m{^\s*(?://|/\*|\*)};
                my $code = $text =~ s{//.*$}{}r;
                push @errors, "unsafe process-locale override in $_:$line\n"
                  if $code =~
/(?:updateConfiguration|Locale\.setDefault|I2PD_JNI\.setLanguage)/;
            }
            close($fh);
        }
    },
    "$root/app/src/main"
);

# Every XML/Java string reference must resolve in the source catalog. The
# lookbehind keeps framework resources such as android.R.string.ok out of the
# check, since only the app's own catalog is authoritative here.
find(
    {
        no_chdir => 1,
        wanted   => sub {
            return unless /\.(?:java|xml)$/;
            open( my $fh, '<', $_ ) or return;
            while ( my $text = <$fh> ) {
                while ( $text =~ /(?<!android\.)R\.string\.([A-Za-z0-9_]+)/g ) {
                    fail("missing string resource R.string.$1 in $_")
                      unless exists $base->{$1};
                }
                while ( $text =~ /(?<!android\.)R\.plurals\.([A-Za-z0-9_]+)/g )
                {
                    fail("missing plural resource R.plurals.$1 in $_")
                      unless exists $base_plurals->{$1};
                }
                while ( $text =~ /\@string\/([A-Za-z0-9_]+)/g ) {
                    fail("missing string resource \@string/$1 in $_")
                      unless exists $base->{$1};
                }
            }
            close($fh);
        }
    },
    "$root/app/src/main"
);

# Instrumentation and JVM tests also reference resources; a removed id would
# only fail on the device lane otherwise.
for my $tree ( "$root/app/src/androidTest", "$root/app/src/test" ) {
    next unless -d $tree;
    find(
        {
            no_chdir => 1,
            wanted   => sub {
                return unless /\.(?:java|xml)$/;
                open( my $fh, '<', $_ ) or return;
                while ( my $text = <$fh> ) {
                    while (
                        $text =~ /(?<!android\.)R\.string\.([A-Za-z0-9_]+)/g )
                    {
                        fail("missing string resource R.string.$1 in $_")
                          unless exists $base->{$1};
                    }
                    while (
                        $text =~ /(?<!android\.)R\.plurals\.([A-Za-z0-9_]+)/g )
                    {
                        fail("missing plural resource R.plurals.$1 in $_")
                          unless exists $base_plurals->{$1};
                    }
                }
                close($fh);
            }
        },
        $tree
    );
}

if (@errors) {
    print STDERR for @errors;
    print STDERR
      "FAIL: Android locale catalogs are incomplete or inconsistent\n";
    exit 1;
}
print
"OK: Android en/es/ru/zh-CN catalogs, placeholders, metadata and locale policy are consistent\n";

=head1 NAME

check-android-locales.pl - verify the Android locale catalogs and policy

=head1 SYNOPSIS

    perl android/tools/check-android-locales.pl

No arguments. The script uses F<android/app/src/main> when it exists and the
current directory otherwise, so it runs either from the repository root or
from F<android/>.

=head1 DESCRIPTION

Compile-free locale contract for the Java 25 Android app. It verifies:

=over 4

=item *

the locale resource directories: only F<values>, F<values-es>, F<values-ru>
and F<values-zh-rCN> are accepted next to the legitimate configuration
qualifiers, so a stray F<values-zh> cannot silently serve Traditional Chinese;

=item *

catalog parity with F<values/strings.xml>, including that a
C<translatable="false"> key is not translated and that no translated key is
missing or extra;

=item *

positional placeholders in the source catalog and in every translation, with
plural resources present in each catalog and using the quantity forms the
language needs (C<one>/C<other>, plus C<few>/C<many> for Russian);

=item *

C<res/xml/locales_config.xml> declaring exactly C<en, es, ru, zh-CN>, the
manifest C<localeConfig> reference and the AppCompat auto-store service, and
the Gradle C<localeFilters> list C<en, es, ru, zh>;

=item *

the Fastlane store-metadata locale directories, the
C<pref_*_values> arrays marked untranslatable, every C<R.string>,
C<R.plurals> and C<@string> reference resolving in the source catalog, and
the absence of a process-wide locale override in production sources.

=back

=head1 OUTPUT AND EXIT STATUS

Each defect is printed on STDERR, followed by a C<FAIL:> summary, and the exit
status is 1. A clean run prints one C<OK:> line on STDOUT and exits 0. A
missing resource directory reports C<FAIL:> on STDOUT and also exits 1; an
unreadable manifest, Gradle file, preferences file or Fastlane metadata
directory aborts through C<die> (typically status 255).

=head1 FILE FORMAT

XML is matched with regular expressions rather than a parser, because only
core modules are allowed: string and plural bodies are compared verbatim and
placeholders are extracted with a positional-aware pattern. Java, XML and C++
sources are scanned for resource references after comment stripping.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, File::Find, File::Spec, 5.016). No Gradle,
AAPT2, Lint or device is involved.

=head1 SECURITY AND PORTABILITY

Read-only and offline: no Gradle invocation, no APK inspection, no network.
Diagnostics name files and resource identifiers only and never contain a
credential or a user-supplied string value.
