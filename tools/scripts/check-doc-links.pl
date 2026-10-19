#!/usr/bin/env perl
# Copyright (c) 2026, David Uhden Collado
# SPDX-License-Identifier: BSD-3-Clause
#
# Static relative-link and image check for the Markdown documentation.
# It never builds, starts or downloads anything. External URLs and fragment
# anchors are intentionally not fetched; paths are checked against the local
# documentation tree.

use strict;
use warnings;
use File::Find;
use File::Spec;
use Cwd qw(abs_path getcwd);

my $docs = shift @ARGV // 'docs';
$docs =~ s{/+$}{};
die "documentation directory not found: $docs\n" unless -d $docs;

my $root = abs_path($docs) or die "cannot resolve $docs\n";
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

my %anchor_cache;

sub slugify {
    my ($heading) = @_;
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
    open my $fh, '<', $path or do {
        $anchor_cache{$path} = \%anchors;
        return \%anchors;
    };
    my $in_fence = 0;
    while (my $line = <$fh>) {
        $in_fence = !$in_fence if $line =~ /^\s*(?:```|~~~)/;
        next if $in_fence;
        next unless $line =~ /^\s{0,3}(#{1,6})\s+(.+?)\s*\z/;
        my $heading = $2;
        my $explicit;
        if ($heading =~ s/\s*\{#([A-Za-z0-9_.:-]+)\}\s*\z//) {
            $explicit = $1;
        }
        $anchors{$explicit} = 1 if defined $explicit;
        my $slug = slugify($heading);
        $anchors{$slug} = 1 if length $slug;
    }
    close $fh;
    $anchor_cache{$path} = \%anchors;
    return \%anchors;
}

my @missing;
my @missing_anchors;
for my $file (sort @files) {
    open my $fh, '<', $file or die "cannot read $file: $!\n";
    my $in_fence = 0;
    my $line_number = 0;
    while (my $line = <$fh>) {
        ++$line_number;
        $in_fence = !$in_fence if $line =~ /^\s*(?:```|~~~)/;
        next if $in_fence;
        while ($line =~ /!?\[[^\]]*\]\(\s*([^)\s]+)(?:\s+"[^"]*")?\s*\)/g) {
            my $target = $1;
            next if $target =~ m{\A(?:[a-z][a-z0-9+.-]*:|//)}i;

            my $fragment;
            $fragment = $1 if $target =~ /#(.*)\z/;
            $target =~ s/[?#].*\z//;
            $target =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;

            my $path;
            if ($target eq '') {
                $path = File::Spec->rel2abs($file);
            } elsif ($target =~ m{\A/}) {
                $path = File::Spec->catfile($root, split m{/}, substr($target, 1));
            } else {
                my (undef, $dir) = File::Spec->splitpath(File::Spec->rel2abs($file));
                $path = File::Spec->catfile($dir, split m{/}, $target);
            }
            unless (-e $path) {
                push @missing, "$file:$line_number: $target";
                next;
            }
            if (defined $fragment && $fragment ne '' && $path =~ /\.md\z/) {
                my $anchors = anchors_for($path);
                push @missing_anchors, "$file:$line_number: $target#$fragment"
                    unless $anchors->{$fragment};
            }
        }
        while ($line =~ /<img\b[^>]*\bsrc=["']([^"']+)["'][^>]*>/gi) {
            my $target = $1;
            next if $target =~ m{\A(?:[a-z][a-z0-9+.-]*:|//|\#)}i;
            $target =~ s/[?#].*\z//;
            next if $target eq '';
            $target =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/ge;
            my (undef, $dir) = File::Spec->splitpath(File::Spec->rel2abs($file));
            my $path = $target =~ m{\A/}
                ? File::Spec->catfile($root, split m{/}, substr($target, 1))
                : File::Spec->catfile($dir, split m{/}, $target);
            push @missing, "$file:$line_number: $target"
                unless -e $path;
        }
    }
    close $fh;
}

if (@missing) {
    print "Missing relative documentation links:\n";
    print "  $_\n" for @missing;
}
if (@missing_anchors) {
    print "Missing documentation anchors:\n";
    print "  $_\n" for @missing_anchors;
}
if (@missing || @missing_anchors) {
    exit 1;
}
print "Documentation links and anchors OK (" . scalar(@files) . " Markdown files)\n";
