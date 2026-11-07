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
my %texts;
for my $file (@files) {
    my $path = File::Spec->catfile( $dir, $file );
    open my $fh, '<', $path or die "cannot read $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    $checked++;
    $texts{$file} = $text;

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
              && $trigger eq 'workflow_dispatch';
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

    # Every `inputs.NAME` reference must be declared by this workflow's own
    # `workflow_dispatch` inputs. GitHub evaluates an undeclared input as an
    # empty value instead of failing the dispatch, so a stale reference (the
    # removed `source_docs` gate is the historical example) would silently
    # disable a job or run it unconditionally.
    my %declared = map { $_ => 1 } declared_dispatch_inputs( \@lines );
    ( my $without_comments = $text ) =~ s/^[ \t]*#.*$//mg;
    push @errors, "$file: interpreter policy requires Perl, not an alternate runtime"
      if $without_comments =~ /\b(?:python[0-9.]*|pypy[0-9.]*|setup-python)\b/i;
    if ($file eq 'agent-maintenance.yml' || $file eq 'validation.yml') {
        my @commands = ('prove -v tests/agent_knowledge_validator/test_validator.t',
            'perl tools/scripts/check-shell-policy.pl');
        push @commands, $file eq 'agent-maintenance.yml'
          ? 'perl scripts/validate-agent-knowledge.pl --scope range'
          : 'perl scripts/validate-agent-knowledge.pl --scope worktree';
        for my $command (@commands) {
            push @errors, "$file: Perl knowledge contract missing: $command"
              if index($without_comments, $command) < 0;
        }
    }
    if ($file eq 'validation.yml') {
        for my $command (
            q{while IFS= read -r -d '' file; do},
            q{mapfile -d '' -t perl_files},
            q{git ls-files -z '*.pl' '*.pm' '*.t'} ) {
            push @errors, "$file: NUL-delimited Perl inventory missing: $command"
              if index($without_comments, $command) < 0;
        }
    }
    if ($file eq 'build-openbsd.yml') {
        # The pinned action throws before its final copyback when its
        # run input fails. Its named rsync shell instead pulls after the
        # command and returns the original command status.
        my ($prepare) = $without_comments =~ /(^      - name: Prepare OpenBSD VM\n.*?)(?=^      - (?:name|uses):|\z)/ms;
        my ($build) = $without_comments =~ /(^      - name: Build and test inside OpenBSD\n.*?)(?=^      - (?:name|uses):|\z)/ms;
        my ($upload) = $without_comments =~ /(^      - name: Upload OpenBSD test logs\n.*?)(?=^      - (?:name|uses):|\z)/ms;
        for my $required ('uses: vmactions/openbsd-vm@', 'copyback: true',
            'sync: rsync', 'custom-shell-name: openbsd') {
            push @errors, "$file: OpenBSD failure copyback missing: prepare $required"
              unless defined $prepare && index($prepare, $required) >= 0;
        }
        push @errors, "$file: OpenBSD failure copyback forbids the action run input"
          if defined $prepare && $prepare =~ /^          run:/m;
        for my $required ('shell: openbsd {0}', 'run: |', 'set -eu',
            'cmake --build build-output', 'ctest --test-dir build-output',
            'build_status=$?', 'exit "$build_status"') {
            push @errors, "$file: OpenBSD failure copyback missing: build $required"
              unless defined $build && index($build, $required) >= 0;
        }
        for my $required ('if: always()', 'uses: actions/upload-artifact@',
            'build-output/Testing', 'configure.log', 'build.log',
            'ctest-openbsd.xml') {
            push @errors, "$file: OpenBSD failure copyback missing: upload $required"
              unless defined $upload && index($upload, $required) >= 0;
        }
    }
    my %referenced;
    while ( $without_comments =~ /\binputs\.([A-Za-z_][A-Za-z0-9_-]*)/g ) {
        $referenced{$1} = 1;
    }
    for my $name ( sort keys %referenced ) {
        push @errors,
          "$file: inputs.$name is not declared under workflow_dispatch"
          unless $declared{$name};
    }
}

# Discover every job id so the split-ownership and opt-in-gate checks work
# on real `jobs:` keys rather than any two-space mapping key elsewhere in
# the file (`permissions:` children are the common false positive).
sub job_blocks {
    my ($lines) = @_;
    my ( %blocks, $current, $in_jobs );
    for my $line (@$lines) {
        if ( $line =~ /^jobs:\s*$/ ) {
            $in_jobs = 1;
            next;
        }
        next unless $in_jobs;
        if ( $line =~ /^\S/ ) {
            $in_jobs  = 0;
            $current = undef;
            next;
        }
        if ( $line =~ /^ {2}([A-Za-z0-9_-]+):\s*$/ ) {
            $current          = $1;
            $blocks{$current} = '';
            next;
        }
        $blocks{$current} .= "$line\n" if defined $current;
    }
    return \%blocks;
}

# The input ids declared directly below `on.workflow_dispatch.inputs`.
sub declared_dispatch_inputs {
    my ($lines) = @_;
    my ( @names, $in_inputs );
    for my $line (@$lines) {
        next if $line =~ /^\s*#/;
        if ( !$in_inputs ) {
            $in_inputs = 1 if $line =~ /^ {4}inputs:\s*$/;
            next;
        }
        if ( $line =~ /^ {6}([A-Za-z0-9_-]+):/ ) {
            push @names, $1;
            next;
        }
        next if $line =~ /^\s*$/;
        last if $line =~ /^ {0,5}\S/;    # dedented out of the inputs map
    }
    return @names;
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
    if ($path =~ m{[\\/]linux-openssl-build[\\/]action[.]ya?ml\z}) {
        # A failed compiler must still leave stderr and probe evidence.
        # Inspect commands and their upload block separately so an unused
        # filename elsewhere cannot satisfy the artifact contract.
        my $active = $joined;
        $active =~ s/^[ \t]*#.*$//mg;
        for my $required (
            q{STRICT_WARNINGS: ${{ inputs.warnings_as_errors }}},
            q{case "$STRICT_WARNINGS" in}, q{ON|OFF)},
            q{-DI2PD_WARNINGS_AS_ERRORS="$STRICT_WARNINGS"} ) {
            push @errors, "$path: strict warnings chain missing: $required"
              if index($active, $required) < 0;
        }
        push @errors, "$path: strict warnings action default must be OFF"
          unless $active =~ /^  warnings_as_errors:\n(?:(?!^  [A-Za-z_]+:).)*?^    default: ['"]?OFF['"]?\s*$/ms;
        for my $phase ( [ 'configure', qr/cmake\s+-S\s+build\s+-B\s+build-output/ ],
            [ 'build', qr/cmake\s+--build\s+build-output/ ] ) {
            my ($name, $command) = @{$phase};
            push @errors, "$path: build evidence missing: $name stderr capture"
              unless $active =~ /$command[^\n]*2>&1\s*\|\s*tee\s+(?:"?\$PWD\/)?\Q$name\E[.]log"?(?:\s|$)/;
        }
        my ($build_block) = $active =~ /(^[ \t]*-[ ]name:[ ]Build and run all CTest targets\n.*?)(?=^[ \t]*-[ ]name:|\z)/ms;
        push @errors, "$path: build evidence missing: pipeline failure propagation"
          unless defined $build_block && $build_block =~ /set -euo pipefail/;
        my ($upload_block) = $active =~ /(^[ \t]*-[ ]name:[^\n]*\n(?:(?!^[ \t]*-[ ]name:).)*?uses:[ ]actions\/upload-artifact[^\n]*\n(?:(?!^[ \t]*-[ ]name:).)*)/ms;
        for my $required ('if: always()', 'configure.log', 'build.log',
            'build-output/CMakeFiles/CMakeConfigureLog.yaml') {
            push @errors, "$path: build evidence missing: upload $required"
              unless defined $upload_block && index($upload_block, $required) >= 0;
        }
    }
    for my $line ( split /\n/, $joined ) {
        next if $line =~ /^\s*#/;
        push @errors, "$path: interpreter policy requires Perl, not an alternate runtime"
          if $line =~ /\b(?:python[0-9.]*|pypy[0-9.]*|setup-python)\b/i;
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

# Regular OpenSSL branch lanes consume bottles. A separate diagnostic
# builds pinned crypto with TSan; it never replaces those matrix lanes.
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
    my $text    = $texts{$file};
    my $formula = $branch_formula{$file};
    my $active = $text;
    $active =~ s/^[ \t]*#.*$//mg;
    push @errors, "$file: strict warnings dispatch must be boolean default false"
      unless $active =~ /^      warnings_as_errors:\n(?:(?!^      [A-Za-z_]+:).)*?^        type: boolean\n(?:(?!^      [A-Za-z_]+:).)*?^        default: false\s*$/ms;
    push @errors, "$file: strict warnings forwarding missing"
      if index($active, q{warnings_as_errors: ${{ inputs.warnings_as_errors && 'ON' || 'OFF' }}}) < 0;
    if ( index( $text, $formula ) < 0 ) {
        push @errors, "$file: does not select the $formula Homebrew bottle";
    }
    if ( $text =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/ ) {
        push @errors, "$file: must use the Homebrew $formula bottle, not a source build";
    }
}
for my $file (@files) {
    next if exists $branch_formula{$file};
    if ( $texts{$file} =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/ ) {
        push @errors, "$file: OpenSSL source builds moved to the Homebrew branch workflows";
    }
}
my $crypto_diagnostic = $texts{'crypto-tsan.yml'};
if (defined $crypto_diagnostic) {
    for my $required (
        'OPENSSL_COMMIT: 2f4e3a5fec05394068cfa7379890ab552483a8a6',
        'https://github.com/openssl/openssl.git',
        'test "$(git -C "$diagnostic/source" rev-parse HEAD)" = "$OPENSSL_COMMIT"',
        'CC=clang CXX=clang++ perl ./Configure', '-fsanitize=thread',
        '--prefix="$diagnostic/prefix"', '--libdir=lib',
        '-DWITH_THREADSANITIZER=ON',
        '-DOPENSSL_CRYPTO_LIBRARY="$diagnostic/prefix/lib/libcrypto.so"',
        '-DOPENSSL_SSL_LIBRARY="$diagnostic/prefix/lib/libssl.so"',
        '--target test-clientcontext-registry',
        q{-R '^test-clientcontext-registry$'},
        'ctest --test-dir "$diagnostic/project" -j 1',
        'readelf -n "$image"', 'sha256sum "$image"',
        'if: always()', 'staged="$GITHUB_WORKSPACE/crypto-tsan-evidence"',
        'cp -R "$diagnostic/evidence" "$staged/logs"',
        'cp -L "$image" "$staged/images/"',
        'path: crypto-tsan-evidence',
        'if-no-files-found: error' ) {
        push @errors, "crypto-tsan.yml: missing instrumented crypto boundary $required"
          if index($crypto_diagnostic, $required) < 0;
    }
    push @errors, 'crypto-tsan.yml: sanitizer suppressions are forbidden'
      if $crypto_diagnostic =~ /suppressions=|ignore_noninstrumented_modules|halt_on_error=0/;
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
my %blocks_for;
for my $file (@files) {
    $blocks_for{$file} = job_blocks( [ split /\n/, $texts{$file} ] );
}
for my $file ( sort keys %split_jobs ) {
    if ( !exists $blocks_for{$file} ) {
        push @errors, "$file: missing split workflow";
        next;
    }
    for my $job ( @{ $split_jobs{$file} } ) {
        if ( !exists $blocks_for{$file}{$job} ) {
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
        if ( exists $blocks_for{$file}{$job} ) {
            push @errors,
              "$file: job '$job' belongs to $job_owner{$job}";
        }
    }
}

# Raw sanitizer PCs require the exact executable, not a later rebuild.
# Keep the known leak/lifecycle reproducers beside their logs and hashes.
if ( exists $blocks_for{'validation.yml'}{'sanitizers'} ) {
    my $sanitizers = $blocks_for{'validation.yml'}{'sanitizers'};
    my $active = $sanitizers;
    $active =~ s/^[ \t]*#.*$//mg;
    $active =~ s/\\\n\s*/ /g;
    for my $phase ( [ 'configure', qr/cmake\s+-S\s+build\s+-B\s+build-sanitizers/ ],
        [ 'build', qr/cmake\s+--build\s+build-sanitizers/ ] ) {
        my ($name, $command) = @{$phase};
        push @errors, "validation.yml: sanitizer build evidence missing: $name stderr capture"
          unless $active =~ /$command[^\n]*2>&1\s*\|\s*tee\s+sanitizer-\Q$name\E[.]log(?:\s|$)/;
    }
    my ($build_block) = $active =~ /(^[ \t]*-[ ]name:[ ]Build and run sanitizer CTest matrix\n.*?)(?=^[ \t]*-[ ]name:|\z)/ms;
    push @errors, "validation.yml: sanitizer build evidence missing: pipeline failure propagation"
      unless defined $build_block && $build_block =~ /set -euo pipefail/;
    my ($upload_block) = $active =~ /(^[ \t]*-[ ]name:[ ]Upload sanitizer test logs\n.*?)(?=^[ \t]*-[ ]name:|\z)/ms;
    for my $required ('if: always()', 'uses: actions/upload-artifact',
        'sanitizer-configure.log', 'sanitizer-build.log',
        'build-sanitizers/CMakeCache.txt',
        'build-sanitizers/CMakeFiles/CMakeConfigureLog.yaml') {
        push @errors, "validation.yml: sanitizer build evidence missing: upload $required"
          unless defined $upload_block && index($upload_block, $required) >= 0;
    }
    for my $image (qw(test-eddsa test-i2ptunnel-stop test-i2ptunnel-targets
        test-eciesx25519-ratchet-session test-runnableservice-lifecycle
        test-i2cp-session-lifecycle)) {
        if ( index( $sanitizers, "build-sanitizers/tests/$image" ) < 0 ) {
            push @errors, "validation.yml: sanitizer executable evidence missing: $image";
        }
    }
    for my $marker ( 'sha256sum', 'sanitizer-evidence/revision.txt',
        'sanitizer-evidence/missing-images.txt' ) {
        if ( index( $sanitizers, $marker ) < 0 ) {
            push @errors, "validation.yml: sanitizer identity evidence missing: $marker";
        }
    }
}

# The expensive opt-in lanes stay opt-in: an Android API 29 emulator boot or
# a Perl::Critic run on every dispatch is a cost regression, and an
# accidental `if:` removal is otherwise invisible until the job starts.
my %input_gate = (
    'build-android.yml' => { 'android-ui' => 'android_ui' },
    'validation.yml'    => { 'perl-quality' => 'perl_style' },
);
for my $file ( sort keys %input_gate ) {
    next unless exists $blocks_for{$file};
    for my $job ( sort keys %{ $input_gate{$file} } ) {
        my $input = $input_gate{$file}{$job};
        if ( !exists $blocks_for{$file}{$job} ) {
            push @errors, "$file: missing opt-in job '$job'";
            next;
        }
        if ( $blocks_for{$file}{$job} !~ /\binputs\.\Q$input\E\b/ ) {
            push @errors,
              "$file: job '$job' must stay gated on inputs.$input";
        }
    }
}

# Crash evidence must not collect process memory or argument values.
# Keep the opt-in debugger command file to this code-location allowlist.
push @errors, windows_crash_command_errors( $texts{'build-windows.yml'} )
  if exists $texts{'build-windows.yml'};

sub windows_crash_command_errors {
    my ($windows) = @_;
    my @violations;
    my ( $inside, $closed ) = ( 0, 0 );
    my %events = map { $_ => 1 } qw(av 0xc0000374 0xc0000409);
    my %seen;
    my %allowed = map { $_ => 1 }
      ( '.echo OPENI2PD_CRASH', '.lastevent', 'ln @rip', 'kc 40', 'q' );
    push @violations, 'build-windows.yml: crash marker must fail the diagnostic step'
      unless index($windows, q{Select-String -Path $log -Pattern '^OPENI2PD_CRASH\s*$' -Quiet}) >= 0
        && index($windows, q{$failures += "$test debugger exception"}) >= 0
        && index($windows, q{$failures += "$test missing debugger log"}) >= 0;
    for my $line ( split /\n/x, $windows ) {
        $line =~ s/\A\s+|\s+\z//gx;
        if ( !$inside && $line eq q{@'} ) {
            $inside = 1;
            next;
        }
        next unless $inside;
        if ( $line eq q{'@ | Set-Content -Encoding ascii $commands} ) {
            $closed = 1;
            last;
        }
        next if !length $line || $line eq 'g';
        my ( $body, $event ) = $line =~ /\A sxe \s+ -c \s+ "([^"]+)" \s+ (\S+) \z/x;
        if ( !defined $body || !$events{$event} ) {
            push @violations, 'build-windows.yml: unsupported crash debugger line';
            next;
        }
        $seen{$event} = 1;
        for my $command ( split /\s*;\s*/x, $body ) {
            push @violations, 'build-windows.yml: crash diagnostics may print code locations only'
              unless $allowed{$command};
        }
    }
    push @violations, 'build-windows.yml: missing bounded crash command file'
      unless $inside && $closed && keys(%seen) == keys(%events);
    return @violations;
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
declare only C<workflow_dispatch>; only
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

=item *

every C<inputs.NAME> reference must be declared by that workflow's own
C<on.workflow_dispatch.inputs>. GitHub substitutes an empty value for an
undeclared input instead of rejecting the dispatch, so a stale reference
silently disables a job or removes its gate;

=item *

the lanes are split by concern and the owning workflow is pinned for
F<build-linux.yml> (C<install-components>, C<interop>), F<build-android.yml>
(C<android>, C<android-ui>) and F<validation.yml> (C<source-docs>,
C<skills-static>, C<perl-quality>, C<sanitizers>, C<fuzz-smoke>). A pinned
job that appears in another workflow fails, and F<build.yml>/F<fuzz.yml> must
not return. The expensive opt-in jobs C<android-ui> and C<perl-quality> must
keep referencing their C<android_ui>/C<perl_style> input gate.

=item *

the literal Windows crash command file must configure the three bounded
exception filters using only event descriptions, code symbols and function
names. Memory dumps, arguments, locals and unsupported commands are refused.
This text contract does not launch a debugger or establish runtime safety.

=back

=head1 FILE FORMAT

Workflows are scanned as text, not parsed as YAML: a third-party YAML module
would be a non-core dependency. Shell/backtick continuations are joined first
so a C<ctest> call split over several lines is still seen as one command, and
lines whose first non-blank character is C<#> are ignored. A trigger list may
be written as a block or as an inline bracketed list. Job ids are read only
from the C<jobs:> mapping at two-space indentation, so a C<permissions:>
child such as C<contents: read> is never mistaken for a job.

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
