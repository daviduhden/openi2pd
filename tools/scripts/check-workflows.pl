#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check workflows.
#
# Compile-free contract for manual build/test workflows and the manual
# agent-maintenance gate: workflow_dispatch is the only trigger, no
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
    local $/ = undef;
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
        for my $trigger (@triggers) {
            $trigger =~ s/:$//;
            next if $trigger eq 'workflow_dispatch';
            push @errors, "$file: unsupported trigger '$trigger'";
        }
        push @errors, "$file: workflow_dispatch is not declared"
          unless grep { $_ eq 'workflow_dispatch' } @triggers;
    }

    # Every `inputs.NAME` reference must be declared by this workflow's own
    # `workflow_dispatch` inputs. GitHub evaluates an undeclared input as an
    # empty value instead of failing the dispatch, so a stale reference (the
    # removed `source_docs` gate is the historical example) would silently
    # disable a job or run it unconditionally.
    my %declared = map { $_ => 1 } declared_dispatch_inputs( \@lines );
    ( my $without_comments = $text ) =~ s/^[ \t]*#.*$//mg;
    push @errors,
      "$file: interpreter policy requires Perl, not an alternate runtime"
      if $without_comments =~ /\b(?:python[0-9.]*|pypy[0-9.]*|setup-python)\b/i;
    if ( $file eq 'agent-maintenance.yml' || $file eq 'validation.yml' ) {
        my @commands = (
            'prove -v tests/agent_knowledge_validator/test_validator.t',
            'perl tools/scripts/check-shell-policy.pl'
        );
        push @commands,
          'perl scripts/validate-agent-knowledge.pl --scope worktree';
        for my $command (@commands) {
            push @errors, "$file: Perl knowledge contract missing: $command"
              if index( $without_comments, $command ) < 0;
        }
    }
    if ( $file eq 'validation.yml' ) {
        for my $command (
            q{while IFS= read -r -d '' file; do},
            q{mapfile -d '' -t perl_files},
            q{git ls-files -z '*.pl' '*.pm' '*.t'}
          )
        {
            push @errors,
              "$file: NUL-delimited Perl inventory missing: $command"
              if index( $without_comments, $command ) < 0;
        }
    }
    if ( $file eq 'build-openbsd.yml' ) {

        # The pinned action throws before its final copyback when its
        # run input fails. Its named rsync shell instead pulls after the
        # command and returns the original command status.
        my ($prepare) = $without_comments =~
/(^      - name: Prepare OpenBSD VM\n.*?)(?=^      - (?:name|uses):|\z)/ms;
        my ($build) = $without_comments =~
/(^      - name: Build and test inside OpenBSD\n.*?)(?=^      - (?:name|uses):|\z)/ms;
        my ($upload) = $without_comments =~
/(^      - name: Upload OpenBSD test logs\n.*?)(?=^      - (?:name|uses):|\z)/ms;
        for my $required (
            'uses: vmactions/openbsd-vm@',
            'copyback: true',
            'sync: rsync',
            'custom-shell-name: openbsd'
          )
        {
            push @errors,
              "$file: OpenBSD failure copyback missing: prepare $required"
              unless defined $prepare && index( $prepare, $required ) >= 0;
        }
        push @errors,
          "$file: OpenBSD failure copyback forbids the action run input"
          if defined $prepare && $prepare =~ /^          run:/m;
        for my $required (
            'shell: openbsd {0}',
            'run: |',
            'set -eu',
            'cmake --build build-output',
            'ctest --test-dir build-output',
            'build_status=$?',
            'exit "$build_status"'
          )
        {
            push @errors,
              "$file: OpenBSD failure copyback missing: build $required"
              unless defined $build && index( $build, $required ) >= 0;
        }
        for my $required (
            'if: always()',         'uses: actions/upload-artifact@',
            'build-output/Testing', 'configure.log',
            'build.log',            'ctest-openbsd.xml'
          )
        {
            push @errors,
              "$file: OpenBSD failure copyback missing: upload $required"
              unless defined $upload && index( $upload, $required ) >= 0;
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
            $in_jobs = 0;
            $current = undef;
            next;
        }
        if ( $line =~ /^ {2}([A-Za-z0-9_-]+):\s*$/ ) {
            $current = $1;
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
    local $/ = undef;
    my $text = <$fh>;
    close $fh;
    ( my $joined = $text ) =~ s/\\\n\s*/ /g;
    $joined =~ s/`\n\s*/ /g;
    if ( $path =~ m{[\\/]linux-openssl-build[\\/]action[.]ya?ml\z} ) {

        # A failed compiler must still leave stderr and probe evidence.
        # Inspect commands and their upload block separately so an unused
        # filename elsewhere cannot satisfy the artifact contract.
        my $active = $joined;
        $active =~ s/^[ \t]*#.*$//mg;
        for my $required (
            q{STRICT_WARNINGS: ${{ inputs.warnings_as_errors }}},
            q{case "$STRICT_WARNINGS" in},
            q{ON|OFF)},
            q{-DI2PD_WARNINGS_AS_ERRORS="$STRICT_WARNINGS"}
          )
        {
            push @errors, "$path: strict warnings chain missing: $required"
              if index( $active, $required ) < 0;
        }
        push @errors, "$path: strict warnings action default must be OFF"
          unless $active =~
/^  warnings_as_errors:\n(?:(?!^  [A-Za-z_]+:).)*?^    default: ['"]?OFF['"]?\s*$/ms;
        for my $phase (
            [ 'configure', qr/cmake\s+-S\s+build\s+-B\s+build-output/ ],
            [ 'build',     qr/cmake\s+--build\s+build-output/ ]
          )
        {
            my ( $name, $command ) = @{$phase};
            push @errors, "$path: build evidence missing: $name stderr capture"
              unless $active =~
/$command[^\n]*2>&1\s*\|\s*tee\s+(?:"?\$PWD\/)?\Q$name\E[.]log"?(?:\s|$)/;
        }
        my ($build_block) = $active =~
/(^[ \t]*-[ ]name:[ ]Build and run all CTest targets\n.*?)(?=^[ \t]*-[ ]name:|\z)/ms;
        push @errors,
          "$path: build evidence missing: pipeline failure propagation"
          unless defined $build_block && $build_block =~ /set -euo pipefail/;
        my ($upload_block) = $active =~
/(^[ \t]*-[ ]name:[^\n]*\n(?:(?!^[ \t]*-[ ]name:).)*?uses:[ ]actions\/upload-artifact[^\n]*\n(?:(?!^[ \t]*-[ ]name:).)*)/ms;
        for my $required (
            'if: always()', 'configure.log',
            'build.log',    'build-output/CMakeFiles/CMakeConfigureLog.yaml'
          )
        {
            push @errors, "$path: build evidence missing: upload $required"
              unless defined $upload_block
              && index( $upload_block, $required ) >= 0;
        }
    }
    for my $line ( split /\n/, $joined ) {
        next if $line =~ /^\s*#/;
        push @errors,
          "$path: interpreter policy requires Perl, not an alternate runtime"
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
    my $active  = $text;
    $active =~ s/^[ \t]*#.*$//mg;
    push @errors,
      "$file: strict warnings dispatch must be boolean default false"
      unless $active =~
/^      warnings_as_errors:\n(?:(?!^      [A-Za-z_]+:).)*?^        type: boolean\n(?:(?!^      [A-Za-z_]+:).)*?^        default: false\s*$/ms;
    push @errors,
      "$file: strict warnings forwarding missing"
      if index( $active,
        q{warnings_as_errors: ${{ inputs.warnings_as_errors && 'ON' || 'OFF' }}}
      ) < 0;
    if ( index( $text, $formula ) < 0 ) {
        push @errors, "$file: does not select the $formula Homebrew bottle";
    }
    if ( $text =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/ ) {
        push @errors,
          "$file: must use the Homebrew $formula bottle, not a source build";
    }
}
for my $file (@files) {
    next if exists $branch_formula{$file};
    if ( $texts{$file} =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/ ) {
        push @errors,
          "$file: OpenSSL source builds moved to the Homebrew branch workflows";
    }
}
my $crypto_diagnostic = $texts{'crypto-tsan.yml'};
my $crypto_action_path =
  File::Spec->catfile( $actions_dir, 'tsan-openssl', 'action.yml' );
open my $crypto_action_file, '<', $crypto_action_path
  or die "cannot read instrumented crypto action: $!\n";
my $crypto_action = do { local $/ = undef; <$crypto_action_file> };
close $crypto_action_file;
if ( defined $crypto_diagnostic ) {
    for my $required (
        'OPENSSL_COMMIT: 2f4e3a5fec05394068cfa7379890ab552483a8a6',
        'https://github.com/openssl/openssl.git',
'test "$(git -C "$diagnostic/source" rev-parse HEAD)" = "$OPENSSL_COMMIT"',
'patch_file="$GITHUB_WORKSPACE/.github/patches/openssl-tsan-compiler-macro.patch"',
        'git apply --check "$patch_file"',
        'git apply "$patch_file"',
        'git diff --binary > "$diagnostic/evidence/openssl-source.patch"',
        'CC=clang CXX=clang++ perl ./Configure',
        '-fsanitize=thread',
        '--prefix="$diagnostic/prefix"',
        '--libdir=lib',
        '-DWITH_THREADSANITIZER=ON',
        '-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -Wl,--build-id=sha1"',
        '-DOPENSSL_CRYPTO_LIBRARY="$diagnostic/prefix/lib/libcrypto.so"',
        '-DOPENSSL_SSL_LIBRARY="$diagnostic/prefix/lib/libssl.so"',
        '--target test-clientcontext-registry',
        q{-R '^test-clientcontext-registry$'},
        'ctest --test-dir "$diagnostic/project" -j 1',
        'readelf -n "$image"',
        'sha256sum "$image"',
        'if: always()',
        'staged="$GITHUB_WORKSPACE/crypto-tsan-evidence"',
        'cp -R "$diagnostic/evidence" "$staged/logs"',
        'cp -L "$image" "$staged/images/"',
        'path: crypto-tsan-evidence',
        'if-no-files-found: error'
      )
    {
        push @errors,
          "crypto-tsan.yml: missing instrumented crypto boundary $required"
          if index( $crypto_diagnostic . $crypto_action, $required ) < 0;
    }
    push @errors, 'crypto-tsan.yml: sanitizer suppressions are forbidden'
      if ( $crypto_diagnostic . $crypto_action ) =~
      /suppressions=|ignore_noninstrumented_modules|halt_on_error=0/;
}
for my $required (
    "if: matrix.name == 'tsan'",
    'uses: ./.github/actions/tsan-openssl',
    'TSAN_OPENSSL_PREFIX: ${{ steps.tsan-crypto.outputs.prefix }}',
    'if [ "$SANITIZER_NAME" = tsan ]; then',
    '"-DOPENSSL_CRYPTO_LIBRARY=$TSAN_OPENSSL_PREFIX/lib/libcrypto.so"',
    '"-DOPENSSL_SSL_LIBRARY=$TSAN_OPENSSL_PREFIX/lib/libssl.so"',
    '"-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread -Wl,--build-id=sha1"',
    '"${sanitizer_flags[@]}" "${crypto_flags[@]}"',
    "if: always() && matrix.name == 'tsan'",
    'sanitizer-evidence/openssl/images',
    'sanitizer-linked-libraries.txt'
  )
{
    push @errors,
      "validation.yml: missing shared TSan crypto boundary $required"
      if index( $texts{'validation.yml'}, $required ) < 0;
}
my $crypto_patch_path = File::Spec->catfile( $root, '.github', 'patches',
    'openssl-tsan-compiler-macro.patch' );
if ( defined $crypto_diagnostic ) {
    if ( open my $patch_file, '<', $crypto_patch_path ) {
        local $/ = undef;
        my $patch = <$patch_file>;
        close $patch_file;
        for my $required (
            'f99bbcd5afff69840f2bfed92458cd5ac092cf91',
            'Index: include/internal/threads_common.h',
            '+#if !defined(__SANITIZE_THREAD__)',
            ' #define __SANITIZE_THREAD__',
            ' #include <sanitizer/tsan_interface.h>'
          )
        {
            push @errors,
              "crypto-tsan.yml: incomplete upstream macro patch $required"
              if index( $patch, $required ) < 0;
        }
    }
    else {
        push @errors, 'crypto-tsan.yml: missing upstream macro patch';
    }
}

# The manual lanes are split by concern: Linux builds and packaging
# checks, Android, and the validation lanes (Doxygen, compile-free agent
# checks, Perl quality, sanitizers and fuzzing). A job must live in its
# owning workflow, and the former aggregate names must not return.
my %split_jobs = (
    'build-linux.yml'   => [qw(install-components interop)],
    'build-android.yml' => [qw(android android-ui)],
    'validation.yml'    =>
      [qw(source-docs skills-static perl-quality sanitizers fuzz-smoke)],
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
            push @errors, "$file: job '$job' belongs to $job_owner{$job}";
        }
    }
}

# Raw sanitizer PCs require the exact executable, not a later rebuild.
# Keep the known leak/lifecycle reproducers beside their logs and hashes.
if ( exists $blocks_for{'validation.yml'}{'sanitizers'} ) {
    my $sanitizers = $blocks_for{'validation.yml'}{'sanitizers'};
    my $active     = $sanitizers;
    $active =~ s/^[ \t]*#.*$//mg;
    $active =~ s/\\\n\s*/ /g;
    for my $phase (
        [ 'configure', qr/cmake\s+-S\s+build\s+-B\s+build-sanitizers/ ],
        [ 'build',     qr/cmake\s+--build\s+build-sanitizers/ ]
      )
    {
        my ( $name, $command ) = @{$phase};
        push @errors,
"validation.yml: sanitizer build evidence missing: $name stderr capture"
          unless $active =~
          /$command[^\n]*2>&1\s*\|\s*tee\s+sanitizer-\Q$name\E[.]log(?:\s|$)/;
    }
    my ($build_block) = $active =~
/(^[ \t]*-[ ]name:[ ]Build and run sanitizer CTest matrix\n.*?)(?=^[ \t]*-[ ]name:|\z)/ms;
    push @errors,
"validation.yml: sanitizer build evidence missing: pipeline failure propagation"
      unless defined $build_block && $build_block =~ /set -euo pipefail/;
    my ($upload_block) = $active =~
/(^[ \t]*-[ ]name:[ ]Upload sanitizer test logs\n.*?)(?=^[ \t]*-[ ]name:|\z)/ms;
    for my $required (
        'if: always()',
        'uses: actions/upload-artifact',
        'sanitizer-configure.log',
        'sanitizer-build.log',
        'build-sanitizers/CMakeCache.txt',
        'build-sanitizers/CMakeFiles/CMakeConfigureLog.yaml'
      )
    {
        push @errors,
          "validation.yml: sanitizer build evidence missing: upload $required"
          unless defined $upload_block
          && index( $upload_block, $required ) >= 0;
    }
    for my $image (
        qw(test-eddsa test-i2ptunnel-stop test-i2ptunnel-targets
        test-eciesx25519-ratchet-session test-runnableservice-lifecycle
        test-i2cp-session-lifecycle)
      )
    {
        if ( index( $sanitizers, "build-sanitizers/tests/$image" ) < 0 ) {
            push @errors,
              "validation.yml: sanitizer executable evidence missing: $image";
        }
    }
    for my $marker (
        'sha256sum',
        'sanitizer-evidence/revision.txt',
        'sanitizer-evidence/missing-images.txt'
      )
    {
        if ( index( $sanitizers, $marker ) < 0 ) {
            push @errors,
              "validation.yml: sanitizer identity evidence missing: $marker";
        }
    }
}

# The expensive opt-in lanes stay opt-in: an Android API 29 emulator boot or
# a Perl::Critic run on every dispatch is a cost regression, and an
# accidental `if:` removal is otherwise invisible until the job starts.
my %input_gate = (
    'build-android.yml' => { 'android-ui'   => 'android_ui' },
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
            push @errors, "$file: job '$job' must stay gated on inputs.$input";
        }
    }
}

# Dependency triplets and Android ABIs use separate naming schemes.
# Check both prefixes and the overlay so a native build cannot resolve a
# different architecture or API floor before Gradle starts.
my %android_native = (
    android      => [ 'arm64-android', 'arm64-v8a', 'build-android' ],
    'android-ui' => [ 'x64-android',   'x86_64',    'build-android-x86_64' ],
);
for my $job ( sort keys %android_native ) {
    if ( !exists $blocks_for{'build-android.yml'}{$job} ) {
        push @errors,
          "build-android.yml: missing Android dependency job '$job'";
        next;
    }
    my ( $triplet, $abi, $build_directory ) = @{ $android_native{$job} };
    my $active = $blocks_for{'build-android.yml'}{$job};
    $active =~ s/^[ \t]*\#[^\n]*$//mgx;
    for my $required (
        '-DVCPKG_TARGET_TRIPLET=' . $triplet,
        q{-DVCPKG_OVERLAY_TRIPLETS="$GITHUB_WORKSPACE/contrib/ci/android"},
        '-DANDROID_ABI=' . $abi,
        '-DANDROID_PLATFORM=android-29',
        q{-DCMAKE_PREFIX_PATH=${{ github.workspace }}/}
        . $build_directory
        . '/vcpkg_installed/'
        . $triplet,
        q{-DBOOST_ROOT=${{ github.workspace }}/}
        . $build_directory
        . '/vcpkg_installed/'
        . $triplet,
        '-Pi2pd.abis=' . $abi,
        '-Pandroid.injected.build.abi=' . $abi,
      )
    {
        push @errors,
"build-android.yml: Android dependency boundary in $job lacks $required"
          if index( $active, $required ) < 0;
    }
    while ( $active =~ /-DVCPKG_TARGET_TRIPLET=([^\s\\]+)/gx ) {
        push @errors,
"build-android.yml: Android dependency boundary has wrong triplet in $job"
          if $1 ne $triplet;
    }
    my $path = File::Spec->catfile( $root, 'contrib', 'ci', 'android',
        "$triplet.cmake" );
    if ( open my $overlay, '<', $path ) {
        my $text = do { local $/ = undef; <$overlay> };
        close $overlay
          or push @errors,
          "build-android.yml: Android dependency boundary cannot close $path";
        $text =~ s/^[ \t]*\#[^\n]*$//mgx;
        my $include = index( $text,
            q{include("$ENV{VCPKG_ROOT}/triplets/} . $triplet . q{.cmake")} );
        my $floor      = index( $text, 'set(VCPKG_CMAKE_SYSTEM_VERSION 29)' );
        my $build_type = index( $text, 'set(VCPKG_BUILD_TYPE release)' );
        push @errors,
"build-android.yml: Android dependency boundary has invalid overlay $triplet"
          if $include < 0
          || $floor <= $include
          || $build_type <= $include;
    }
    else {
        push @errors,
"build-android.yml: Android dependency boundary lacks overlay $triplet";
    }
}

# The Java 25 probe must consume the same R8 dependency as AGP.
if ( exists $texts{'build-android.yml'} ) {
    my ($probe) = $texts{'build-android.yml'} =~
      /      - name: Probe Java 25 D8\/R8 dexing\n(.*?)(?=      - name:|\z)/s;
    for my $required (
        '(cd android && ./gradlew --no-daemon writeJava25R8Classpath)',
        'R8_JAR=$(cat android/build/reports/java25/r8-classpath.txt)',
        'test -f "$R8_JAR"'
      )
    {
        push @errors, 'build-android.yml: Java 25 probe lacks AGP-resolved R8'
          if !defined($probe) || index( $probe, $required ) < 0;
    }
    push @errors,
      'build-android.yml: Java 25 probe must not scan caches or SDKs'
      if defined($probe) && $probe =~ /\bfind\s/;
}

# Crash evidence must not collect process memory or argument values.
# Keep the opt-in debugger command file to this code-location allowlist.
push @errors, windows_crash_command_errors( $texts{'build-windows.yml'} )
  if exists $texts{'build-windows.yml'};
push @errors, windows_startup_command_errors( $texts{'build-windows.yml'} )
  if exists $texts{'build-windows.yml'};
push @errors, windows_startup_probe_errors( $texts{'build-windows.yml'} )
  if exists $texts{'build-windows.yml'};

sub windows_startup_command_errors {
    my ($windows) = @_;
    my @violations;
    my ($step) = $windows =~
/(      - name: Capture bounded router loader and entry evidence\n.*?)(?=      - name:)/s;
    return 'build-windows.yml: missing startup diagnostic step'
      unless defined $step;
    my ($commands) = $step =~
/          \@\(\n(.*?)          \) \| Set-Content -Encoding ascii \$commands/s;
    my @expected = (
        q{.echo OPENI2PD_LOADER},
q{sxe -c ".echo OPENI2PD_STARTUP_EXCEPTION; .lastevent; ln @rip; k 20; q" av},
q{sxe -c ".echo OPENI2PD_STARTUP_EXCEPTION; .lastevent; ln @rip; k 20; q" 0xc0000142},
        q{bu user32!MessageBoxA ".echo OPENI2PD_MODAL; ? @$ra; k 20; q"},
        q{bu user32!MessageBoxW ".echo OPENI2PD_MODAL; ? @$ra; k 20; q"},
        q{bp @$exentry ".echo OPENI2PD_ENTRY; ln @rip; k 20; g"},
        'g',
    );
    my @actual =
      defined($commands)
      ? map { /^\s*'([^']*)'\s*,?\s*$/ ? $1 : 'INVALID' }
      split /\n/, $commands
      : ();
    push @violations,
      'build-windows.yml: startup diagnostics may print code locations only'
      unless join( "\n", @actual ) eq join( "\n", @expected );

    for my $required (
        q{if: ${{ always() && inputs.heap_diagnostics }}},
        q{$debugger.WaitForExit(20000)},
        q{& taskkill.exe /PID $debugger.Id /T /F},
q{$debugArguments = @('-G', '-noshell', '-logo', "`"$log`"", '-cf', "`"$commands`"", "`"$binary`"", '--version')},
      )
    {
        push @violations,
          'build-windows.yml: missing bounded startup diagnostic contract'
          if index( $step, $required ) < 0;
    }
    push @violations,
      'build-windows.yml: startup diagnostics must preserve CTest'
      unless index( $windows,
        q{if: ${{ always() && steps.msys2_build.outcome == 'success' }}} ) >= 0;
    push @violations, 'build-windows.yml: missing startup diagnostic artifact'
      unless $windows =~ /^\s*build-output\/startup-debugger\s*$/m;
    return @violations;
}

sub windows_crash_command_errors {
    my ($windows) = @_;
    my @violations;
    push @violations,
      'build-windows.yml: missing bounded worker initialization diagnostic'
      unless $windows =~
/^\s*\$debugArguments = \@\('-G', '-noshell', '-lines', '-y', "`"\$symbols`"", '-logo', "`"\$log`"", '-cf', "`"\$commands`"", "`"\$binary`""\)\s*$/m
      && $windows =~
      /foreach \(\$test in \@\([^\n]*'test-privsep-windows-supervisor'\)\)/
      && $windows =~
/^\s*build-msvc\/tests\/Release\/test-privsep-windows-supervisor[.]exe\s*$/m;
    push @violations,
      'build-windows.yml: crash marker must fail the diagnostic step'
      unless index( $windows,
        q{Select-String -Path $log -Pattern '^OPENI2PD_CRASH\s*$' -Quiet} ) >= 0
      && index( $windows, q{$failures += "$test debugger exception"} ) >= 0
      && index( $windows, q{$failures += "$test missing debugger log"} ) >= 0;
    push @violations, windows_exception_command_errors($windows);
    return @violations;
}

# Validate each supplied exception-command block independently.
sub windows_exception_command_errors {
    my ( $windows, $probe_mode ) = @_;
    my @violations;
    my ( $inside, $closed ) = ( 0, 0 );
    my %events = map { $_ => 1 } qw(av 0xc0000374 0xc0000409 0xc0000142);
    my %seen;

    # Only the nonce-free probe may inspect termination status. Exact
    # lines keep nested breakpoint commands outside the heap allowlist.
    my %probe_commands = map { $_ => 1 } (
q{sxe -c "bu ntdll!NtTerminateProcess \".if (@edx == 0xc0000142) { .echo OPENI2PD_PROBE_TERMINATION; ln @rip; kc 40; }; gc\"; g" cpr},
        q{sxe -c ".echo OPENI2PD_PROBE_EXIT; .lastevent; g" epr}
    );
    my %probe_seen;
    my %allowed = map { $_ => 1 }
      ( '.echo OPENI2PD_CRASH', '.lastevent', 'ln @rip', 'kc 40', 'q' );
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
        if ( $probe_mode && $probe_commands{$line} ) {
            $probe_seen{$line}++;
            next;
        }
        my ( $body, $event ) =
          $line =~ /\A sxe \s+ -c \s+ "([^"]+)" \s+ (\S+) \z/x;
        if ( !defined $body || !$events{$event} ) {
            push @violations,
              'build-windows.yml: unsupported crash debugger line';
            next;
        }
        $seen{$event} = 1;
        for my $command ( split /\s*;\s*/x, $body ) {
            push @violations,
'build-windows.yml: crash diagnostics may print code locations only'
              unless $allowed{$command};
        }
    }
    push @violations, 'build-windows.yml: missing bounded crash command file'
      unless $inside && $closed && keys(%seen) == keys(%events);
    push @violations,
      'build-windows.yml: startup probe termination evidence missing'
      if $probe_mode
      && ( keys(%probe_seen) != 2 || grep { $_ != 1 } values %probe_seen );
    return @violations;
}

sub windows_startup_probe_errors {
    my ($windows) = @_;
    my @violations;
    my ($probe) = $windows =~
/      - name: Capture contained worker initialization\n(.*?)(?=      - name:|\z)/s;
    return 'build-windows.yml: missing bounded startup probe'
      unless defined $probe;
    my @command_files =
      $windows =~ /^\s*'\@ \| Set-Content -Encoding ascii \$commands\s*$/mg;
    my @debugger_launches = $windows =~ /Start-Process -FilePath \$cdb\b/g;
    push @violations,
      'build-windows.yml: startup probe command-file inventory changed'
      unless @command_files == 2 && @debugger_launches == 3;
    my $arguments =
q{$probeArguments = @('-g', '-G', '-o', '-noshell', '-lines', '-y', "`"$symbols`"", '-logo', "`"$log`"", '-cf', "`"$commands`"", "`"$binary`"", '--diagnose-worker-initialization')};
    push @violations,
'build-windows.yml: startup probe must trace only the closed diagnostic mode'
      unless index( $probe, $arguments ) >= 0;
    my @child_lines = $windows =~ /^\s*(\$\w+ = \@\([^\n]*'-o'[^\n]*)$/mg;
    push @violations,
      'build-windows.yml: startup probe cannot trace authenticated workers'
      unless @child_lines == 1 && $child_lines[0] eq $arguments;

    for my $required (
        q{if: ${{ always() && steps.msvc_tests.outcome == 'failure' }}},
        q{Get-FileHash -Algorithm SHA256 -Path $binary},
        q{WaitForExit(60000)},
        q{taskkill.exe /PID $debugger.Id /T /F},
        q{Select-String -Path $log -Pattern '^OPENI2PD_CRASH\s*$' -Quiet},
q{Select-String -Path @($log, $consoleLog, $consoleError) -Pattern '^startup probe: exit=0\b' -Quiet},
        q{build-msvc/tests/Release/test-privsep-windows-supervisor.exe}
      )
    {
        push @violations,
          'build-windows.yml: startup probe evidence boundary missing'
          if index( $probe, $required ) < 0;
    }
    push @violations, 'build-windows.yml: startup probe lacks retained artifact'
      unless $windows =~ /^\s*build-msvc\/startup-probe\s*$/m;
    push @violations, windows_exception_command_errors( $probe, 1 );
    return @violations;
}

if (@errors) {
    print STDERR "$_\n" for @errors;
    exit 1;
}

print "workflow-static: $checked workflow(s) and "
  . scalar(@action_files)
  . " action file(s) obey the manual-only trigger policy and fail on empty CTest selections\n";
exit 0;

=head1 NAME

check-workflows.pl - enforce manual-only GitHub Actions policies

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

an F<on:> block is required. Every workflow, including
F<agent-maintenance.yml>, must declare only C<workflow_dispatch>. Push,
pull-request, schedule, repository-dispatch and workflow-call triggers fail
the check;

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

Android dependencies use vcpkg triplets C<arm64-android> and C<x64-android>.
Their Android ABI names remain C<arm64-v8a> and C<x86_64>, respectively.
Both jobs must select matching install prefixes and repository overlays
that pin API 29 after including the upstream triplet. The checker reads
those overlay files without configuring CMake or running vcpkg.

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
