#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check doc nav.
#
# Static MkDocs publication check: navigation targets and theme assets must
# exist, no HTML documentation may remain, and every Markdown file must be
# navigable or explicitly allowlisted. It never runs MkDocs.

use strict;
use warnings;
use File::Find qw(find);
use File::Spec;

my $root = shift @ARGV // '.';
my $docs = File::Spec->catdir($root, 'docs');
my $config = File::Spec->catfile($docs, 'mkdocs.yml');
die "missing $config\n" unless -f $config;

open my $fh, '<', $config or die "open $config: $!\n";
my (@nav, $in_nav);
while (my $line = <$fh>) {
    $in_nav = 1 if $line =~ /^nav:\s*$/;
    last if $in_nav && $line =~ /^\S/ && $line !~ /^nav:/;
    next unless $in_nav;
    push @nav, $1 if $line =~ /:\s+([A-Za-z0-9_.\/-]+\.md)\s*$/;
}
close $fh;

my @missing;
for my $entry (@nav) {
    my $path = File::Spec->catfile($docs, split m{/}, $entry);
    push @missing, "nav target missing: $entry" unless -f $path;
}

my %navigated = map { $_ => 1 } @nav;
my %allowed = map { $_ => 1 } qw(
    README.md
    UPSTREAM-README.md
    file-headers.md
    assets/README.md
    assets/branding/colors.md
    devs/building/ios.md
    devs/building/qt-desktop-gui.md
    devs/building/haikuos.md
);
my @unlisted;
find({
    no_chdir => 1,
    wanted => sub {
        return unless -f $_ && /\.md\z/;
        my $rel = File::Spec->abs2rel($File::Find::name, $docs);
        push @unlisted, $rel unless $navigated{$rel} || $allowed{$rel};
    },
}, $docs);

my @html;
find({
    no_chdir => 1,
    wanted => sub { push @html, $File::Find::name if -f $_ && /\.(?:html?|xhtml)\z/i; },
}, $docs);

for my $asset (qw(img/logo.png img/favicon.ico)) {
    push @missing, "theme asset missing: $asset"
        unless -f File::Spec->catfile($docs, split m{/}, $asset);
}

for my $problem (@missing, map { "unlisted Markdown: $_" } @unlisted,
    map { "HTML documentation remains: $_" } @html) {
    print STDERR "$problem\n";
}
exit 1 if @missing || @unlisted || @html;
print "Documentation navigation OK (" . scalar(@nav) . " entries, " . scalar(@unlisted) . " allowlisted)\n";
