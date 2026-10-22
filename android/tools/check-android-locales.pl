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
use File::Find qw(find);

my @languages = qw(en es ru zh-CN);
my %language_dir = ('en' => 'values', 'es' => 'values-es', 'ru' => 'values-ru', 'zh-CN' => 'values-zh-rCN');
my %allowed_store = map { $_ => 1 } qw(en-US es-ES ru-RU zh-CN);
my $root = -d 'android/app/src/main' ? 'android' : '.';
my $res = "$root/app/src/main/res";
my @errors;

sub fail { push @errors, join('', @_) . "\n" }
sub read_file {
    my ($path) = @_;
    open(my $fh, '<:raw', $path) or die "open $path: $!";
    local $/;
    my $text = <$fh>;
    close($fh);
    fail("BOM in $path") if $text =~ /^\xEF\xBB\xBF/;
    fail("CRLF in $path") if $text =~ /\r\n/;
    return $text;
}

sub catalog {
    my ($path) = @_;
    my $xml = read_file($path);
    my (%strings, %attrs);
    while ($xml =~ /<string\s+([^>]*?)>(.*?)<\/string>/sg) {
        my ($raw_attrs, $value) = ($1, $2);
        next unless $raw_attrs =~ /\bname="([^"]+)"/;
        my $name = $1;
        fail("duplicate string $name in $path") if exists $strings{$name};
        $strings{$name} = $value;
        $attrs{$name} = $raw_attrs;
    }
    return (\%strings, \%attrs);
}

sub placeholders {
    my ($value) = @_;
    my @matches = ($value =~ /%(?:\d+\$)?[-+ 0#]*\d*(?:\.\d+)?[sd]/g);
    return join(',', sort @matches);
}

if (!-d $res) {
    print "FAIL: Android resource directory not found\n";
    exit 1;
}
opendir(my $dh, $res) or die "opendir($res): $!";
my @value_dirs = grep { /^values(?:-|$)/ } readdir($dh);
closedir($dh);
for my $dir (sort @value_dirs) {
    next if $dir eq 'values';
    next if $dir =~ /^values-(?:night|sw\d+dp|w\d+dp|v\d+)$/;
    fail("unsupported locale resource directory: res/$dir") unless $dir eq 'values-es' || $dir eq 'values-ru' || $dir eq 'values-zh-rCN';
}

my $base_path = "$res/values/strings.xml";
my ($base, $base_attrs) = catalog($base_path);
my %translations;
for my $tag (qw(es ru zh-CN)) {
    my $path = "$res/$language_dir{$tag}/strings.xml";
    fail("missing locale catalog $path") unless -f $path;
    next unless -f $path;
    my ($translated, $attrs) = catalog($path);
    $translations{$tag} = $translated;
    for my $name (sort keys %$base) {
        next if $base_attrs->{$name} =~ /translatable="false"/;
        fail("missing $tag string $name") unless exists $translated->{$name};
    }
    for my $name (sort keys %$translated) {
        fail("extra $tag string $name") unless exists $base->{$name};
        fail("translatable=false key $name present in $tag") if $base_attrs->{$name} =~ /translatable="false"/;
        next unless exists $base->{$name};
        my $want = placeholders($base->{$name});
        my $got = placeholders($translated->{$name});
        fail("placeholder mismatch for $tag:$name ($want != $got)") if $want ne $got;
    }
}

if (!-f "$res/xml/locales_config.xml") {
    fail("missing res/xml/locales_config.xml");
} else {
    my $xml = read_file("$res/xml/locales_config.xml");
    my @locales = ($xml =~ /<locale\s+android:name="([^"]+)"/g);
    fail("locales_config must declare en, es, ru, zh-CN") unless join(',', @locales) eq join(',', @languages);
}

my $manifest = read_file("$root/app/src/main/AndroidManifest.xml");
fail("AndroidManifest.xml does not declare localeConfig") unless $manifest =~ /android:localeConfig="\@xml\/locales_config"/;
fail("AndroidManifest.xml lacks AppCompat auto-store service") unless $manifest =~ /AppLocalesMetadataHolderService/;

my $gradle = read_file("$root/app/build.gradle");
if ($gradle =~ /androidResources\s*\{(.*?)\}/s) {
    my $block = $1;
    my @filters = ($block =~ /localeFilters\s*\+=\s*\[([^\]]*)\]/s);
    my $joined = join('', @filters);
    my @tags = ($joined =~ /["']([^"']+)["']/g);
    fail("Gradle localeFilters must be en, es, ru, zh") unless join(',', @tags) eq 'en,es,ru,zh';
} else {
    fail("Gradle androidResources block missing");
}

opendir(my $fdh, "$root/fastlane/metadata/android") or die "open fastlane metadata: $!";
my @store_dirs = grep { /^[A-Za-z]/ && -d "$root/fastlane/metadata/android/$_" } readdir($fdh);
closedir($fdh);
for my $dir (@store_dirs) {
    fail("unsupported Fastlane locale $dir") unless $allowed_store{$dir};
}

my $prefs = read_file("$res/values/preferences_i2pd_options.xml");
for my $array (qw(pref_language_values pref_bandwidth_values pref_loglevel_values)) {
    fail("$array must be translatable=false") unless $prefs =~ /<string-array\s+name="$array"[^>]*translatable="false"/s;
}

find({
    no_chdir => 1,
    wanted => sub {
        return unless /\.(?:java|cpp|h)$/;
        open(my $fh, '<', $_) or return;
        my $line = 0;
        while (my $text = <$fh>) {
            ++$line;
            next if $text =~ m{^\s*(?://|/\*|\*)};
            my $code = $text =~ s{//.*$}{}r;
            push @errors, "unsafe process-locale override in $_:$line\n"
                if $code =~ /(?:updateConfiguration|Locale\.setDefault|I2PD_JNI\.setLanguage)/;
        }
        close($fh);
    }
}, "$root/app/src/main");

# Every XML/Java string reference must resolve in the source catalog.
find({
    no_chdir => 1,
    wanted => sub {
        return unless /\.(?:java|xml)$/;
        open(my $fh, '<', $_) or return;
        while (my $text = <$fh>) {
            while ($text =~ /(?<!android\.)R\.string\.([A-Za-z0-9_]+)/g) {
                fail("missing string resource R.string.$1 in $_") unless exists $base->{$1};
            }
            while ($text =~ /\@string\/([A-Za-z0-9_]+)/g) {
                fail("missing string resource \@string/$1 in $_") unless exists $base->{$1};
            }
        }
        close($fh);
    }
}, "$root/app/src/main");

if (@errors) {
    print for @errors;
    print "FAIL: Android locale catalogs are incomplete or inconsistent\n";
    exit 1;
}
print "OK: Android en/es/ru/zh-CN catalogs, placeholders, metadata and locale policy are consistent\n";
