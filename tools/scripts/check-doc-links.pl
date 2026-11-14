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
use File::Basename qw(basename dirname);
use File::Spec;
use Cwd    qw(abs_path);
use Encode qw(encode decode FB_CROAK);

# Filesystem operations use UTF-8 octets; Markdown and fragments stay
# Unicode. Never concatenate decoded link text with byte-valued parents.
sub path_octets {
    my ($path) = @_;
    return $path unless defined $path && utf8::is_utf8($path);
    return encode( 'UTF-8', $path, FB_CROAK );
}

# Diagnostics use Unicode independently of filesystem byte paths. Escape
# undecodable names rather than emitting malformed UTF-8 or mojibake.
sub display_path {
    my ($path) = @_;
    return $path if utf8::is_utf8($path);
    my $copy = $path;
    my $text = eval { decode( 'UTF-8', $copy, FB_CROAK ) };
    return $text unless $@;
    return join '', map { $_ < 128 ? chr($_) : sprintf '\\x%02X', $_ }
      unpack 'C*', $path;
}

binmode STDERR, ':encoding(UTF-8)' or die "cannot encode diagnostics\n";
my $docs = path_octets( shift @ARGV // 'docs' );
$docs =~ s{/+$}{};
die "documentation directory not found: " . display_path($docs) . "\n" unless -d $docs;

my $root = path_octets( abs_path($docs) )
  or die "cannot resolve " . display_path($docs) . "\n";
my $repo_root = path_octets( abs_path( File::Spec->catdir( $root, File::Spec->updir ) ) );
die "cannot resolve repository root\n" unless defined $repo_root;

# Cwd::abs_path() returns forward-slash paths on Windows while
# File::Spec->catdir()/splitdir() use the native separator, so a raw
# string prefix rejects every valid link on that platform. Both sides are
# reduced to the same key: canonical components joined with '/', absolute
# paths marked with a leading '/', and folded to lower case where the
# filesystem is case-insensitive. The key is a comparison value only; the
# filesystem is always accessed through the original path.
sub path_key {
    my ($path)    = @_;
    my $canonical = File::Spec->canonpath($path);
    my $key       = join '/', grep { length } File::Spec->splitdir($canonical);
    $key = "/$key" if File::Spec->file_name_is_absolute($canonical);
    $key = lc $key if $^O eq 'MSWin32' || $^O eq 'msys';
    return $key;
}

my $repo_key = path_key($repo_root);

# A sibling such as "<repo>-old" shares the textual prefix but not a path
# component, so containment requires the separator boundary. A resolved
# target equal to the repository root (the documentation directory is the
# root here) is inside as well.
sub is_inside_repository {
    my ($resolved) = @_;
    my $key = path_key($resolved);
    return 1 if $key eq $repo_key;
    return index( $key, "$repo_key/" ) == 0;
}

# Resolve existing prefixes physically before reducing missing suffixes.
# canonpath alone deliberately preserves '..', so it cannot prove that
# an absent target stays below the repository. Existing symlinks retain
# their filesystem meaning before any following parent component.
sub resolved_target {
    my ( $path, $depth ) = @_;
    $depth //= 0;
    return undef if $depth > 40;
    my $candidate = File::Spec->rel2abs($path);
    my @suffix;
    my $physical;
    while ( !defined( $physical = path_octets( abs_path($candidate) ) ) ) {
        my $parent = dirname($candidate);
        return undef if $parent eq $candidate;
        unshift @suffix, basename($candidate);
        $candidate = $parent;
    }
    for my $part (@suffix) {
        next if $part eq '' || $part eq '.';
        if ( $part eq '..' ) {
            $physical = dirname($physical);
            next;
        }
        $candidate = File::Spec->catfile( $physical, $part );
        if ( -l $candidate ) {
            my $target = path_octets( readlink $candidate );
            return undef unless defined $target;
            $target = File::Spec->catfile( dirname($candidate), $target )
              unless File::Spec->file_name_is_absolute($target);
            $physical = resolved_target( $target, $depth + 1 );
            return undef unless defined $physical;
            next;
        }
        my $resolved = path_octets( abs_path($candidate) );
        $physical = defined $resolved ? $resolved : $candidate;
    }
    return $physical;
}

my @files;
find(
    {
        wanted => sub {
            return unless -f $_ && /\.md\z/;
            push @files, path_octets($File::Find::name);
        },
        no_chdir => 1,
    },
    $docs
);

die "no Markdown files under " . display_path($docs) . "\n" unless @files;

my %anchor_cache;

# Decode before both Unicode slug generation and link comparison. A raw
# byte stream makes punctuation and case-folding depend on UTF-8 bytes.
sub markdown_lines {
    my ($path) = @_;
    my $shown_path = display_path($path);
    open my $fh, '<:raw', $path or die "cannot read Markdown file: $shown_path\n";
    my $bytes = '';
    while (1) {
        my $count = read( $fh, my $chunk, 65536 );
        die "cannot read Markdown file: $shown_path\n" unless defined $count;
        last                                     unless $count;
        $bytes .= $chunk;
    }
    close $fh or die "cannot close Markdown file: $shown_path\n";
    my $text = eval { decode( 'UTF-8', $bytes, FB_CROAK ) };
    die "invalid UTF-8 in Markdown file: $shown_path\n" if $@;
    my @lines = split /(?<=\n)/, $text;
    return \@lines;
}

# URL escapes carry bytes, not Unicode code points. Keep literal Unicode
# and escaped UTF-8 in one strict encoding before comparing or resolving.
sub local_component {
    my ($component) = @_;
    return ( undef, 'invalid percent escape in local link' )
      if $component =~ /%(?![0-9A-Fa-f]{2})/;
    my $bytes = encode( 'UTF-8', $component, FB_CROAK );
    $bytes =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;
    my $decoded = eval { decode( 'UTF-8', $bytes, FB_CROAK ) };
    return ( undef,    'invalid UTF-8 in local link component' ) if $@;
    return ( undef,    'NUL in local link component' ) if $decoded =~ /\0/;
    return ( $decoded, undef );
}

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
    my $in_fence = 0;
    my $previous = '';
    for my $line ( @{ markdown_lines($path) } ) {
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
    $anchor_cache{$path} = \%anchors;
    return \%anchors;
}

my @missing;
my @missing_anchors;
for my $file ( sort @files ) {
    my $shown_file = display_path($file);
    my $in_fence    = 0;
    my $line_number = 0;
    for my $line ( @{ markdown_lines($file) } ) {
        ++$line_number;
        $in_fence = !$in_fence if $line =~ /^\s*(?:```|~~~)/;
        next                   if $in_fence;
        while ( $line =~ /!?\[[^\]]*\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\)/g ) {
            my $target = $1;
            next if $target =~ m{\A(?:[a-z][a-z0-9+.-]*:|//)}i;

            my $fragment;
            $fragment = $1 if $target =~ /#(.*)\z/;
            $target =~ s/[?#].*\z//;
            my $component_error;
            ( $target, $component_error ) = local_component($target);
            if ( !$component_error && defined $fragment ) {
                ( $fragment, $component_error ) = local_component($fragment);
            }
            if ($component_error) {
                push @missing, "$shown_file:$line_number: $component_error";
                next;
            }

            my $filesystem_target = path_octets($target);
            my $path;
            if ( $target eq '' ) {
                $path = File::Spec->rel2abs($file);
            }
            elsif ( $target =~ m{\A/} ) {
                $path = File::Spec->catfile( $root, split m{/},
                    substr( $filesystem_target, 1 ) );
            }
            else {
                my ( undef, $dir ) =
                  File::Spec->splitpath( File::Spec->rel2abs($file) );
                $path = File::Spec->catfile( $dir, split m{/}, $filesystem_target );
            }
            my $resolved = resolved_target($path);
            if ( !defined $resolved ) {
                push @missing,
                  "$shown_file:$line_number: $target (unresolvable path)";
                next;
            }
            if ( !is_inside_repository($resolved) ) {
                push @missing,
                  "$shown_file:$line_number: $target (escapes repository)";
                next;
            }

            # Resolution classifies containment, but a missing directory
            # before '..' must not become a valid existing link target.
            unless ( -e $path ) {
                push @missing, "$shown_file:$line_number: $target";
                next;
            }
            if ( defined $fragment && $fragment ne '' && $path =~ /\.md\z/ ) {
                my $anchors = anchors_for($path);
                push @missing_anchors, "$shown_file:$line_number: $target#$fragment"
                  unless $anchors->{$fragment};
            }
        }
        while ( $line =~ /<img\b[^>]*\bsrc=["']([^"']+)["'][^>]*>/gi ) {
            my $target = $1;
            next if $target =~ m{\A(?:[a-z][a-z0-9+.-]*:|//|\#)}i;
            $target =~ s/[?#].*\z//;
            next if $target eq '';
            my $component_error;
            ( $target, $component_error ) = local_component($target);
            if ($component_error) {
                push @missing, "$shown_file:$line_number: $component_error";
                next;
            }
            my $filesystem_target = path_octets($target);
            my ( undef, $dir ) =
              File::Spec->splitpath( File::Spec->rel2abs($file) );
            my $path =
              $target =~ m{\A/}
              ? File::Spec->catfile( $root, split m{/}, substr( $filesystem_target, 1 ) )
              : File::Spec->catfile( $dir,  split m{/}, $filesystem_target );
            my $resolved = resolved_target($path);

            if ( !defined $resolved ) {
                push @missing,
                  "$shown_file:$line_number: $target (unresolvable path)";
                next;
            }
            push @missing, "$shown_file:$line_number: $target (escapes repository)"
              unless is_inside_repository($resolved);
            push @missing, "$shown_file:$line_number: $target"
              if is_inside_repository($resolved) && !-e $path;
        }
    }
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
Markdown target against the anchors of the destination page. Fragment-only
Markdown links are checked against the current page. Absolute URLs and
protocol-relative URLs are neither fetched nor resolved; fragment-only HTML
image sources are ignored. A target that resolves outside the repository
(for example through C<..>) is reported as missing.

Markdown is decoded as strict UTF-8 before link parsing and Unicode heading
case-folding. Local paths, image targets and fragments decode percent escapes
as UTF-8 bytes; malformed escapes, invalid UTF-8 and NUL components are
validation errors. Literal Unicode and percent-encoded Unicode share this
same comparison model; no legacy non-UTF-8 URL encoding is accepted.

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
through C<die>. Invalid UTF-8 Markdown also aborts explicitly. Malformed
percent escapes, non-UTF-8 URL components and NUL components are validation
errors (exit 1), reported by file and line without echoing the component.

=head1 LIMITS

Fenced code blocks are skipped, but inline code spans are not: a link-like
literal between backticks is still validated and may be reported missing.
Reference-style definitions (C<[label]: path>) and HTML anchors other than
C<< <img src> >> are outside the model, so they are neither validated nor
reported.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, File::Find, File::Basename, File::Spec,
Cwd, Encode, 5.016), which the OpenBSD base interpreter provides.

=head1 SECURITY AND PORTABILITY

Read-only and offline. The checker never follows a link to the network and
writes nothing except STDOUT/STDERR.

Containment is decided on a separator-normalised key built from
C<File::Spec-E<gt>canonpath> and C<File::Spec-E<gt>splitdir>, not on a raw
string prefix: C<Cwd::abs_path> returns forward slashes on Windows while
C<File::Spec-E<gt>catdir> uses the native separator, so a prefix comparison
would report every valid link as escaping the repository there. The key is
case-folded on case-insensitive filesystems, keeps the absolute-path marker,
and requires a path-component boundary so a sibling directory that shares the
prefix is still rejected. Existing prefixes are resolved physically before
normalising missing suffixes. Symbolic links retain their meaning before
following C<..> components, including when the final target is absent.
Unreadable links and excessive recursive resolution fail closed.
