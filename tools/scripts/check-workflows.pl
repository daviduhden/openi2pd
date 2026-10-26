#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check workflows.
#
# Compile-free contract for the manual GitHub Actions workflows: only
# workflow_dispatch/workflow_call triggers, no continue-on-error, and every
# executing ctest invocation must fail on an empty selection. It never
# dispatches, parses YAML with a third-party module or runs a workflow.
#

use strict;
use warnings;
use 5.016;

use File::Spec;
use FindBin qw($Bin);

my $root = File::Spec->rel2abs(
    File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir ) );
my $dir = File::Spec->catdir( $root, '.github', 'workflows' );

opendir my $dh, $dir or die "cannot open $dir: $!\n";
my @files = sort grep { /\.ya?ml\z/ } readdir $dh;
closedir $dh;
die "no workflow files in $dir\n" unless @files;

my @errors;
my $checked = 0;
for my $file (@files) {
    my $path = File::Spec->catfile( $dir, $file );
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    $checked++;

    # Join shell/command continuations so a ctest invocation split across
    # lines is checked as one logical command.
    ( my $joined = $text ) =~ s/\\\n\s*/ /g;
    $joined =~ s/`\n\s*/ /g;
    my @lines = split /\n/, $joined;

    my $in_on   = 0;
    my $seen_on = 0;
    my @triggers;
    for my $line (@lines) {
        next if $line =~ /^\s*#/;
        if ( $line =~ /^on:\s*$/ ) {
            $in_on   = 1;
            $seen_on = 1;
            next;
        }
        if ( $line =~ /^on:\s*(\S.*)$/ ) {
            $seen_on = 1;
            my $inline = $1;
            $inline =~ s/^\s*\[//;
            $inline =~ s/\]\s*$//;
            push @triggers, grep { /\S/ } split /\s*,\s*/, $inline;
            $in_on = 0;
            next;
        }
        if ($in_on) {
            if ( $line =~ /^\S/ ) {
                $in_on = 0;
                next;
            }
            push @triggers, $1 if $line =~ /^\s{2}([A-Za-z0-9_-]+):/;
            next;
        }
        if ( $line =~ /^\s*continue-on-error\s*:/ ) {
            push @errors, "$file: continue-on-error is forbidden";
        }
        if (   $line =~ /(?:^|\s)ctest\s+--/
            && $line !~ /\s-N(?:\s|$)/
            && $line !~ /--print-labels/ )
        {
            push @errors, "$file: ctest invocation lacks --no-tests=error"
              unless $line =~ /--no-tests=error/;
            push @errors, "$file: ctest invocation lacks --output-junit"
              unless $line =~ /--output-junit/;
        }
    }

    if ( !$seen_on ) {
        push @errors, "$file: missing on: block";
    }
    else {
        for my $trigger (@triggers) {
            $trigger =~ s/:$//;
            next
              if $trigger eq 'workflow_dispatch' || $trigger eq 'workflow_call';
            push @errors, "$file: unsupported trigger '$trigger'";
        }
        push @errors, "$file: workflow_dispatch is not declared"
          unless grep { $_ eq 'workflow_dispatch' } @triggers;
    }
}

if (@errors) {
    print STDERR "$_\n" for @errors;
    exit 1;
}

print
"workflow-static: $checked workflow(s) are manual-only and fail on empty CTest selections\n";
exit 0;
