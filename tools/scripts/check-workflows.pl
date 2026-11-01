#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check workflows.
#
# Compile-free contract for manual build/test workflows and the
# dedicated pull-request agent-maintenance gate: no other triggers, no
# continue-on-error, and every executing ctest invocation must fail on
# an empty selection. It never dispatches, parses YAML with a
# third-party module or runs a workflow.
#

use strict;
use warnings;
use 5.016;

use File::Find ();
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

    # Join shell/command continuations so a ctest invocation split
    # across lines is checked as one logical command.
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

        # Only executing invocations are constrained. The two exclusions
        # are query-only forms: -N lists tests without running them and
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
            next
              if !$maintenance_gate
              && ( $trigger eq 'workflow_dispatch'
                || $trigger eq 'workflow_call' );
            push @errors, "$file: unsupported trigger '$trigger'";
        }
        if ($maintenance_gate) {
            push @errors, "$file: maintenance gate must declare pull_request"
              unless grep { $_ eq 'pull_request' } @triggers;
            push @errors,
              "$file: maintenance gate must not run on workflow_dispatch"
              if grep { $_ eq 'workflow_dispatch' } @triggers;
        }
        else {
            push @errors, "$file: workflow_dispatch is not declared"
              unless grep { $_ eq 'workflow_dispatch' } @triggers;
        }
    }
}

# Composite actions under .github/actions can execute ctest too, so the
# same empty-selection and JUnit contract must cover them; they have no
# trigger block to inspect.
my $actions_dir = File::Spec->catdir( $root, '.github', 'actions' );
my @action_files;
if ( -d $actions_dir ) {
    File::Find::find(
        {
            no_chdir => 1,
            wanted   => sub {
                push @action_files, $_
                  if -f $_ && $_ =~ /\.ya?ml\z/;
            },
        },
        $actions_dir
    );
}
for my $path ( sort @action_files ) {
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    ( my $joined = $text ) =~ s/\\\n\s*/ /g;
    $joined =~ s/`\n\s*/ /g;
    for my $line ( split /\n/, $joined ) {
        next if $line =~ /^\s*#/;
        if ( $line =~ /^\s*continue-on-error\s*:/ ) {
            push @errors, "$path: continue-on-error is forbidden";
        }
        if (   $line =~ /(?:^|\s)ctest\s+--/
            && $line !~ /\s-N(?:\s|$)/
            && $line !~ /--print-labels/ )
        {
            push @errors, "$path: ctest invocation lacks --no-tests=error"
              unless $line =~ /--no-tests=error/;
            push @errors, "$path: ctest invocation lacks --output-junit"
              unless $line =~ /--output-junit/;
        }
    }
}

# The two Linux OpenSSL branch lanes are the only Linux build workflows.
# They must consume the Homebrew precompiled bottle for the branch they
# claim (openssl@3 / openssl@4) and must not reintroduce a source-built
# pinned OpenSSL download: the split exists to keep the matrix small and
# fast while still exercising the latest version of each branch.
my %branch_formula = (
    'build-linux-openssl3.yml' => 'openssl@3',
    'build-linux-openssl4.yml' => 'openssl@4',
);
for my $file ( sort keys %branch_formula ) {
    my $path = File::Spec->catfile( $dir, $file );
    if ( !-f $path ) {
        push @errors, "$file: missing Linux OpenSSL branch workflow";
        next;
    }
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    my $formula = $branch_formula{$file};
    if ( index( $text, $formula ) < 0 ) {
        push @errors, "$file: does not select the $formula Homebrew bottle";
    }
    if ( $text =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/ ) {
        push @errors, "$file: must use the Homebrew $formula bottle, not a source build";
    }
}
for my $file (@files) {
    next if exists $branch_formula{$file};
    my $path = File::Spec->catfile( $dir, $file );
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    if ( $text =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/ ) {
        push @errors, "$file: OpenSSL source builds moved to the Homebrew branch workflows";
    }
}
# The manual lanes are split by concern: Linux builds and packaging
# checks, Android, and the validation lanes (Doxygen, compile-free agent
# checks, Perl quality, sanitizers and fuzzing). A job must live in its
# owning workflow, and the former aggregate names must not return.
my %split_jobs = (
    'build-linux.yml'   => [qw(install-components interop)],
    'build-android.yml' => [qw(android android-ui)],
    'validation.yml'    => [qw(source-docs skills-static perl-quality sanitizers fuzz-smoke)],
);
my %job_owner;
for my $file ( sort keys %split_jobs ) {
    my $path = File::Spec->catfile( $dir, $file );
    if ( !-f $path ) {
        push @errors, "$file: missing split workflow";
        next;
    }
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    for my $job ( @{ $split_jobs{$file} } ) {
        if ( $text !~ /^  \Q$job\E:\s*$/m ) {
            push @errors, "$file: missing job '$job'";
        }
        $job_owner{$job} = $file;
    }
}
for my $legacy (qw(build.yml fuzz.yml)) {
    if ( -f File::Spec->catfile( $dir, $legacy ) ) {
        push @errors, "$legacy: the aggregate workflow was split by concern";
    }
}
for my $job ( sort keys %job_owner ) {
    for my $file (@files) {
        next if $file eq $job_owner{$job};
        my $path = File::Spec->catfile( $dir, $file );
        open my $fh, '<', $path or die "cannot read $path: $!\n";
        local $/;
        my $text = <$fh>;
        close $fh;
        if ( $text =~ /^  \Q$job\E:\s*$/m ) {
            push @errors,
              "$file: job '$job' belongs to $job_owner{$job}";
        }
    }
}

if (@errors) {
    print STDERR "$_\n" for @errors;
    exit 1;
}

print
"workflow-static: $checked workflow(s) and " . scalar (@action_files) . " action file(s) obey the manual-build/PR-maintenance trigger policy and fail on empty CTest selections\n";
exit 0;

=head1 NAME

check-workflows.pl - enforce manual-build and PR-maintenance GitHub Actions policies

=head1 SYNOPSIS

    perl tools/scripts/check-workflows.pl

No arguments. The script locates the repository from its own path, so it must
stay at F<tools/scripts/check-workflows.pl> inside a checkout; a relocated copy
aborts because the workflow directory cannot be opened.

=head1 DESCRIPTION

Read-only contract check for everything under F<.github/workflows/> and the
composite action files under F<.github/actions/>:

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

every C<ctest> invocation, in a workflow or in a composite action, must pass
C<--no-tests=error> and C<--output-junit>; C<-N> and C<--print-labels> are
exempt because they only list tests or labels and never execute a selection.
C<continue-on-error> is likewise refused in both.

=item *

F<build-linux-openssl3.yml> and F<build-linux-openssl4.yml> must exist and
select the matching Homebrew C<openssl@3>/C<openssl@4> bottle. A pinned
source build (C<openssl.org/source>, C<OPENSSL_SHA256>, C<OPENSSL_PIN>) is
refused so the reduced Linux matrix cannot silently return to compiling
OpenSSL itself.

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
