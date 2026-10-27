#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check workflows.
#
# Compile-free contract for manual build/test workflows and the dedicated
# pull-request agent-maintenance gate: no other triggers, no continue-on-error, and every
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
        # Only executing invocations are constrained. The two exclusions are
        # query-only forms: -N lists tests without running them and
        # --print-labels only reports the label inventory.
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
        my $maintenance_gate = $file eq 'agent-maintenance.yml';
        for my $trigger (@triggers) {
            $trigger =~ s/:$//;
            next if $maintenance_gate && $trigger eq 'pull_request';
            next if !$maintenance_gate
              && ( $trigger eq 'workflow_dispatch' || $trigger eq 'workflow_call' );
            push @errors, "$file: unsupported trigger '$trigger'";
        }
        if ($maintenance_gate) {
            push @errors, "$file: maintenance gate must declare pull_request"
              unless grep { $_ eq 'pull_request' } @triggers;
            push @errors, "$file: maintenance gate must not run on workflow_dispatch"
              if grep { $_ eq 'workflow_dispatch' } @triggers;
        }
        else {
            push @errors, "$file: workflow_dispatch is not declared"
              unless grep { $_ eq 'workflow_dispatch' } @triggers;
        }
    }
}

if (@errors) {
    print STDERR "$_\n" for @errors;
    exit 1;
}

print
"workflow-static: $checked workflow(s) obey the manual-build/PR-maintenance trigger policy and fail on empty CTest selections\n";
exit 0;

=head1 NAME

check-workflows.pl - enforce manual-build and PR-maintenance GitHub Actions policies

=head1 SYNOPSIS

    perl tools/scripts/check-workflows.pl

No arguments. The script locates the repository from its own path, so it must
stay at F<tools/scripts/check-workflows.pl> inside a checkout; a relocated copy
aborts because the workflow directory cannot be opened.

=head1 DESCRIPTION

Read-only contract check for everything under F<.github/workflows/>:

=over 4

=item *

an F<on:> block is required. All build, test and packaging workflows must
declare C<workflow_dispatch> (optionally C<workflow_call>); only
F<agent-maintenance.yml> may declare C<pull_request>, and it may not also
declare C<workflow_dispatch>. Push and schedule triggers fail the check;

=item *

C<continue-on-error> is refused at job and step level, because suppressing a
failure is not a valid way to make a lane green;

=item *

every C<ctest> invocation must pass C<--no-tests=error> and
C<--output-junit>; C<-N> and C<--print-labels> are exempt because they only
list tests or labels and never execute a selection.

=back

=head1 FILE FORMAT

Workflows are scanned as text, not parsed as YAML: a third-party YAML module
would be a non-core dependency. Shell/backtick continuations are joined first
so a C<ctest> call split over several lines is still seen as one command, and
lines whose first non-blank character is C<#> are ignored. A trigger list may
be written as a block or as an inline bracketed list.

=head1 OUTPUT AND EXIT STATUS

Each violation is printed on STDERR as C<< <file>: <message> >> and the script
exits 1. A clean run prints one summary line on STDOUT and exits 0. An
unreadable workflow directory or an empty workflow set aborts through C<die>.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, File::Spec, FindBin, 5.016). The OpenBSD
base interpreter is sufficient.

=head1 SECURITY AND PORTABILITY

Read-only and offline: no workflow is dispatched, no token is read and no
network request is made. The check is platform-independent text processing.
