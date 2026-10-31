#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check doc links.
#
# Static checker for relative links, images and heading anchors in the
# Markdown documentation tree. It never fetches external URLs and exits
# non-zero when a local target or anchor is missing.
#

use strict;
use warnings;
use 5.016;
use File::Find;
use File::Spec;
use Cwd qw(abs_path);

my $docs = shift @ARGV // 'docs';
$docs =~ s{/+$}{};
die "documentation directory not found: $docs\n" unless -d $docs;

my $root      = abs_path($docs) or die "cannot resolve $docs\n";
my $repo_root = abs_path( File::Spec->catdir( $root, File::Spec->updir ) );
die "cannot resolve repository root\n" unless defined $repo_root;
# catdir appends a trailing separator. The stripped value is therefore a
# directory prefix ending in the platform separator, which index() can
# compare against so a sibling such as "<repo>-old" is not accepted as
# inside the tree.
my $repo_prefix = File::Spec->catdir( $repo_root, 'x' );
$repo_prefix =~ s/x\z//;
my @files;
find(
    {
        wanted => sub {
            return unless -f $_ && /\.md\z/;
            push @files, $File::Find::name;
        },
        no_chdir => 1,
    },
    $docs
);

die "no Markdown files under $docs\n" unless @files;

my %anchor_cache;

sub slugify {
    my ($heading) = @_;
    # Must stay identical to the MkDocs/Python-Markdown slug rules; any
    # divergence turns every valid intra-page link into a false report.
    my $slug = lc $heading;
    $slug =~ s/<[^>]+>//g;
    $slug =~ s/`//g;
    $slug =~ s/[^\p{L}\p{N}\s_-]//g;
    $slug =~ s/\s+/-/g;
    $slug =~ s/-+/-/g;
    $slug =~ s/^-+|-+\z//g;
    return $slug;
}

sub anchors_for {
    my ($path) = @_;
    return $anchor_cache{$path} if exists $anchor_cache{$path};

    my %anchors;
    my %seen_slug;
    my $add_heading = sub {
        my ($heading) = @_;
        # MkDocs honours an explicit trailing {#id} as well as the
        # derived slug, and appends -1, -2 ... to repeated headings.
        # Both forms must be recorded or a valid link is reported
        # missing.
        my $explicit;
        if ( $heading =~ s/\s*\{#([A-Za-z0-9_.:-]+)\}\s*\z// ) {
            $explicit = $1;
        }
        $anchors{$explicit} = 1 if defined $explicit;
        my $base = slugify($heading);
        return unless length $base;
        my $slug = $base;
        $slug = "$base-" . $seen_slug{$base} if $seen_slug{$base}++;
        $anchors{$slug} = 1;
    };
    open my $fh, '<', $path or do {
        $anchor_cache{$path} = \%anchors;
        return \%anchors;
    };
    my $in_fence = 0;
    my $previous = '';
    while ( my $line = <$fh> ) {
        $in_fence = !$in_fence if $line =~ /^\s*(?:```|~~~)/;
        next                   if $in_fence;
        if ( $line =~ /^\s{0,3}#{1,6}\s+(.+?)\s*\z/ ) {
            $add_heading->($1);
            $previous = '';
            next;
        }
        if (   $line =~ /^\s{0,3}(?:=+|-+)\s*\z/
            && $previous =~ /\S/
            && $previous !~ /^\s*(?:[-*+]|\d+\.)\s/
            && $previous !~ /^\s*\|/ )
        {
            # A setext underline promotes the previous line to a
            # heading. Table rows and list items share the '-' character
            # and are excluded.
            $add_heading->($previous);
            $previous = '';
            next;
        }
        $previous = $line if $line =~ /\S/;
    }
    close $fh;
    $anchor_cache{$path} = \%anchors;
    return \%anchors;
}

my @missing;
my @missing_anchors;
for my $file ( sort @files ) {
    open my $fh, '<', $file or die "cannot read $file: $!\n";
    my $in_fence    = 0;
    my $line_number = 0;
    while ( my $line = <$fh> ) {
        ++$line_number;
        $in_fence = !$in_fence if $line =~ /^\s*(?:```|~~~)/;
        next                   if $in_fence;
        while ( $line =~ /!?\[[^\]]*\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\)/g ) {
            my $target = $1;
            next if $target =~ m{\A(?:[a-z][a-z0-9+.-]*:|//)}i;

            my $fragment;
            $fragment = $1 if $target =~ /#(.*)\z/;
            $target =~ s/[?#].*\z//;
            $target =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;
            if ( $target =~ /\0/ ) {
                push @missing, "$file:$line_number: $target (NUL in target)";
                next;
            }

            my $path;
            if ( $target eq '' ) {
                $path = File::Spec->rel2abs($file);
            }
            elsif ( $target =~ m{\A/} ) {
                $path = File::Spec->catfile( $root, split m{/},
                    substr( $target, 1 ) );
            }
            else {
                my ( undef, $dir ) =
                  File::Spec->splitpath( File::Spec->rel2abs($file) );
                $path = File::Spec->catfile( $dir, split m{/}, $target );
            }
            my $resolved = abs_path($path);
            $resolved = File::Spec->canonpath($path) unless defined $resolved;
            if ( $resolved ne $repo_root
                && index( $resolved, $repo_prefix ) != 0 )
            {
                push @missing,
                  "$file:$line_number: $target (escapes repository)";
                next;
            }
            unless ( -e $resolved ) {
                push @missing, "$file:$line_number: $target";
                next;
            }
            if ( defined $fragment && $fragment ne '' && $path =~ /\.md\z/ ) {
                my $anchors = anchors_for($path);
                push @missing_anchors, "$file:$line_number: $target#$fragment"
                  unless $anchors->{$fragment};
            }
        }
        while ( $line =~ /<img\b[^>]*\bsrc=["']([^"']+)["'][^>]*>/gi ) {
            my $target = $1;
            next if $target =~ m{\A(?:[a-z][a-z0-9+.-]*:|//|\#)}i;
            $target =~ s/[?#].*\z//;
            next if $target eq '';
            $target =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;
            if ( $target =~ /\0/ ) {
                push @missing, "$file:$line_number: $target (NUL in target)";
                next;
            }
            my ( undef, $dir ) =
              File::Spec->splitpath( File::Spec->rel2abs($file) );
            my $path =
              $target =~ m{\A/}
              ? File::Spec->catfile( $root, split m{/}, substr( $target, 1 ) )
              : File::Spec->catfile( $dir,  split m{/}, $target );
            my $resolved = abs_path($path);
            $resolved = File::Spec->canonpath($path) unless defined $resolved;
            push @missing, "$file:$line_number: $target (escapes repository)"
              if $resolved ne $repo_root
              && index( $resolved, $repo_prefix ) != 0;
            push @missing, "$file:$line_number: $target"
              if ( $resolved eq $repo_root
                || index( $resolved, $repo_prefix ) == 0 )
              && !-e $resolved;
        }
    }
    close $fh;
}

if (@missing) {
    print STDERR "Missing relative documentation links:\n";
    print STDERR "  $_\n" for @missing;
}
if (@missing_anchors) {
    print STDERR "Missing documentation anchors:\n";
    print STDERR "  $_\n" for @missing_anchors;
}
if ( @missing || @missing_anchors ) {
    exit 1;
}
print "Documentation links and anchors OK ("
  . scalar(@files)
  . " Markdown files)\n";

=head1 NAME

check-doc-links.pl - static relative-link and heading-anchor checker

=head1 SYNOPSIS

    perl tools/scripts/check-doc-links.pl [DOCS_DIR]

DOCS_DIR defaults to F<docs> and must exist.

=head1 DESCRIPTION

Walks the Markdown tree, validates every inline link and C<< <img src> >>
target that points inside the repository, and validates the C<#fragment> of a
Markdown target against the anchors of the destination page. Absolute URLs,
protocol-relative URLs and fragment-only links are neither fetched nor
resolved. A target that resolves outside the repository (for example through
C<..>) and a target containing a NUL byte are reported as missing.

=head1 ANCHOR MODEL

Anchors are derived the way MkDocs derives them: lower-case, HTML tags and
backticks removed, everything except letters, digits, whitespace, underscore
and hyphen removed, whitespace collapsed to C<->, repeated hyphens squeezed
and leading/trailing hyphens trimmed. Setext headings are recognised too.
Explicit C<{#id}> suffixes and the C<-1>, C<-2> disambiguation of repeated
headings are recorded so a link to either form resolves.

=head1 OUTPUT AND EXIT STATUS

Problems are grouped under a heading on STDERR, one C<file:line: target> entry
each, and the checker exits 1. A clean run prints a one-line summary on
STDOUT and exits 0. An unreadable Markdown file or a missing DOCS_DIR aborts
through C<die>.

=head1 LIMITS

Fenced code blocks are skipped, but inline code spans are not: a link-like
literal between backticks is still validated and may be reported missing.
Reference-style definitions (C<[label]: path>) and HTML anchors other than
C<< <img src> >> are outside the model, so they are neither validated nor
reported.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, File::Find, File::Spec, Cwd, 5.016), which
the OpenBSD base interpreter provides.

=head1 SECURITY AND PORTABILITY

Read-only and offline. The checker never follows a link to the network and
writes nothing except STDOUT/STDERR.
