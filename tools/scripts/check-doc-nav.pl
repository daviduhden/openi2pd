#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check doc nav.
#
# Static MkDocs publication check: navigation targets and theme assets
# must exist, no HTML documentation may remain, and every Markdown file
# must be navigable or explicitly allowlisted. It never runs MkDocs.

use strict;
use warnings;
use 5.016;
use File::Find qw(find);
use File::Spec;

my $root   = shift @ARGV // '.';
my $docs   = File::Spec->catdir( $root, 'docs' );
my $config = File::Spec->catfile( $docs, 'mkdocs.yml' );
die "missing $config\n" unless -f $config;

open my $fh, '<', $config or die "open $config: $!\n";
my ( @nav, $in_nav );
# Only the indented `- Title: page.md` values matter; the nav block ends
# at the next top-level key. Indentation depth is ignored so nested
# sections are covered without needing a YAML parser.
while ( my $line = <$fh> ) {
    $in_nav = 1 if $line =~ /^nav:\s*$/;
    last if $in_nav && $line =~ /^\S/ && $line !~ /^nav:/;
    next unless $in_nav;
    push @nav, $1 if $line =~ /:\s+([A-Za-z0-9_.\/-]+\.md)\s*$/;
}
close $fh;

my @missing;
for my $entry (@nav) {
    my $path = File::Spec->catfile( $docs, split m{/}, $entry );
    push @missing, "nav target missing: $entry" unless -f $path;
}

my %navigated = map { $_ => 1 } @nav;
my %allowed   = map { $_ => 1 } qw(
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
find(
    {
        no_chdir => 1,
        wanted   => sub {
            return unless -f $_ && /\.md\z/;
            my $rel = File::Spec->abs2rel( $File::Find::name, $docs );
            push @unlisted, $rel unless $navigated{$rel} || $allowed{$rel};
        },
    },
    $docs
);

my @html;
find(
    {
        no_chdir => 1,
        wanted   => sub {
            push @html, $File::Find::name if -f $_ && /\.(?:html?|xhtml)\z/i;
        },
    },
    $docs
);

for my $asset (qw(img/logo.png img/favicon.ico)) {
    push @missing, "theme asset missing: $asset"
      unless -f File::Spec->catfile( $docs, split m{/}, $asset );
}

for my $problem (
    @missing,
    map { "unlisted Markdown: $_" } @unlisted,
    map { "HTML documentation remains: $_" } @html
  )
{
    print STDERR "$problem\n";
}
exit 1 if @missing || @unlisted || @html;
print "Documentation navigation OK ("
  . scalar(@nav)
  . " entries, "
  . scalar(@unlisted)
  . " allowlisted)\n";

=head1 NAME

check-doc-nav.pl - verify the MkDocs navigation, allowlist and theme assets

=head1 SYNOPSIS

    perl tools/scripts/check-doc-nav.pl [ROOT]

ROOT defaults to the current directory and must contain F<docs/mkdocs.yml>.

=head1 DESCRIPTION

Static publication check. It collects the Markdown targets of the top-level
C<nav:> block, verifies that each one exists under F<ROOT/docs>, reports every
Markdown file that is neither navigated nor in the built-in allowlist (README
files, asset notes and build-platform pages that MkDocs renders without a nav
entry), and rejects C<.html>/C<.htm>/C<.xhtml> files in the documentation
tree. The theme assets F<docs/img/logo.png> and F<docs/img/favicon.ico> must
exist because F<mkdocs.yml> references them.

=head1 OUTPUT AND EXIT STATUS

One diagnostic per problem on STDERR, followed by a one-line summary on
STDOUT. Exit 0 when clean, 1 when anything was reported. A missing or
unreadable F<mkdocs.yml> aborts through C<die>, so a wrong ROOT cannot be
mistaken for a clean tree.

=head1 FILE FORMAT

F<mkdocs.yml> is scanned line by line instead of being parsed as YAML: only
indented C<- Title: page.md> values inside C<nav:> are collected, and the block
ends at the first line that starts in column 1. A quoted target, a trailing
inline comment or a flow-style nav is therefore not recognised, and the pages
it lists are reported as unlisted. Indentation depth is irrelevant, so nested
sections are covered.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, File::Find, File::Spec, 5.016), so the check
runs with the OpenBSD base interpreter and needs no YAML module.

=head1 SECURITY AND PORTABILITY

Read-only and offline: no network access, no MkDocs run, nothing written.
Paths are reported relative to ROOT.
