#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check english only.
#
# Compile-free policy check that scans res/, the manifest, app/build.gradle,
# Fastlane metadata and executable source for language overrides, then verifies
# every R.string and @string reference resolves in the English catalog.
# Documentation comments are ignored by the source scan.
#

use strict;
use warnings;
use File::Find qw(find);

my $root = -d 'android/app/src/main' ? 'android' : '.';
my $res = "$root/app/src/main/res";
my @errors;

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
    # A language qualifier is a two/three-letter language optionally followed
    # by a region. BCP-47 b+ locales are rejected as well.
    push @errors, "language resource directory remains: res/$dir\n"
        if $dir =~ /^values-(?:[a-z]{2,3}(?:-r[A-Z]{2})?|b\+)/;
}

if (!-f "$res/xml/locales_config.xml") {
    push @errors, "missing res/xml/locales_config.xml\n";
} else {
    open(my $fh, '<', "$res/xml/locales_config.xml") or die "open locales_config: $!";
    local $/;
    my $xml = <$fh>;
    close($fh);
    my @locales = ($xml =~ /<locale\s+android:name="([^"]+)"/g);
    push @errors, "locales_config must declare exactly English\n"
        unless @locales == 1 && $locales[0] eq 'en';
}

open(my $mfh, '<', "$root/app/src/main/AndroidManifest.xml") or die "open manifest: $!";
local $/;
my $manifest = <$mfh>;
close($mfh);
push @errors, "AndroidManifest.xml does not declare localeConfig\n"
    unless $manifest =~ /android:localeConfig="\@xml\/locales_config"/;

open(my $gfh, '<', "$root/app/build.gradle") or die "open app/build.gradle: $!";
my $gradle = <$gfh>;
close($gfh);
push @errors, "Gradle does not limit androidResources.localeFilters to en\n"
    unless $gradle =~ /localeFilters\s*\+=\s*\[\s*["']en["']\s*\]/s;

opendir(my $fdh, "$root/fastlane/metadata/android") or die "open fastlane metadata: $!";
my @store_dirs = grep { /^[A-Za-z]/ && -d "$root/fastlane/metadata/android/$_" } readdir($fdh);
closedir($fdh);
for my $dir (sort @store_dirs) {
    push @errors, "localized Fastlane listing remains: $dir\n" unless $dir eq 'en-US';
}

find({
    no_chdir => 1,
    wanted => sub {
        return unless /\.(?:java|cpp|h)$/;
        open(my $fh, '<', $_) or return;
        my $line = 0;
        while (my $text = <$fh>) {
            ++$line;
            # Documentation comments may explain why a locale override is not
            # used; only executable source counts for this policy.
            next if $text =~ m{^\s*(?://|/\*|\*)};
            my $code = $text =~ s{//.*$}{}r;
            push @errors, "language override in $_:$line\n"
                if $code =~ /(?:setApplicationLocales|updateConfiguration|Locale\.setDefault|getDisplayLanguage|I2PD_JNI\.setLanguage)/;
        }
        close($fh);
    }
}, "$root/app/src/main");

# Check that every Java/XML reference still resolves in the English catalog.
my %defined;
open(my $sfh, '<', "$res/values/strings.xml") or die "open strings.xml: $!";
while (my $line = <$sfh>) {
    $defined{$1} = 1 while $line =~ /<string\s+name="([^"]+)"/g;
}
close($sfh);
find({
    no_chdir => 1,
    wanted => sub {
        return unless /\.(?:java|xml)$/;
        open(my $fh, '<', $_) or return;
        while (my $text = <$fh>) {
            while ($text =~ /R\.string\.([A-Za-z0-9_]+)/g) {
                push @errors, "missing string resource R.string.$1 in $_\n" unless $defined{$1};
            }
            while ($text =~ /\@string\/([A-Za-z0-9_]+)/g) {
                push @errors, "missing string resource \@string/$1 in $_\n" unless $defined{$1};
            }
        }
        close($fh);
    }
}, "$root/app/src/main");

if (@errors) {
    print for @errors;
    print "FAIL: Android resources are not English-only or references are missing\n";
    exit 1;
}
print "OK: Android resources, references, locale metadata and Fastlane listings are English-only\n";
