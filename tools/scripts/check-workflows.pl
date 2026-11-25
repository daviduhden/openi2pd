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

{
    my $root = File::Spec->rel2abs(
        File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir ) );
    my $dir = File::Spec->catdir( $root, '.github', 'workflows' );

    opendir my $dh, $dir or die "cannot open $dir: $!\n";
    my @files = sort grep { /\.ya?ml\z/x } readdir $dh;
    closedir $dh;
    die "no workflow files in $dir\n" unless @files;

    my @errors;
    my $checked = 0;
    my %texts;
    my $actions_dir = File::Spec->catdir( $root, '.github', 'actions' );
    for my $file (@files) {
        my $path = File::Spec->catfile( $dir, $file );
        open my $fh, '<', $path or die "cannot read $path: $!\n";
        my $text = do { local $/ = undef; <$fh> };
        close $fh;
        $checked++;
        $texts{$file} = $text;

        ( my $joined = $text ) =~ s/\\\n\s*/ /gx;
        $joined =~ s/`\n\s*/ /gx;
        my @lines = split /\n/x, $joined;
        push @errors, trigger_and_ctest_errors( $file, \@lines );
        push @errors, workflow_perl_errors( $file, $text );
        push @errors, openbsd_copyback_errors($text)
          if $file eq 'build-openbsd.yml';
        push @errors, undeclared_input_errors( $file, $text, \@lines );
    }
    my @action_files = action_files($actions_dir);
    for my $path ( sort @action_files ) {
        open my $fh, '<', $path or die "cannot read $path: $!\n";
        my $text = do { local $/ = undef; <$fh> };
        close $fh;

        push @errors, linux_build_action_errors( $path, $text )
          if $path =~ m{[\\/]linux-openssl-build[\\/]action[.]ya?ml\z}x;
        push @errors, action_command_errors( $path, $text );
    }
    push @errors, openssl_branch_errors( $dir, \%texts, \@files );
    push @errors, crypto_tsan_errors( $root, $actions_dir, \%texts );
    my %blocks_for =
      map { $_ => job_blocks( [ split /\n/x, $texts{$_} ] ) } @files;
    push @errors, split_job_errors( $dir, \@files, \%blocks_for );
    push @errors,
      sanitizer_evidence_errors( $blocks_for{'validation.yml'}{'sanitizers'} )
      if exists $blocks_for{'validation.yml'}{'sanitizers'};
    push @errors, opt_in_errors( \%blocks_for );
    if ( my $android = $blocks_for{'build-android.yml'} ) {
        push @errors, android_ui_errors( $android->{'android-ui'} // '' );
        push @errors, android_signing_errors($android);
    }
    push @errors, android_native_errors( $root, \%blocks_for );
    push @errors, android_java_probe_errors( $texts{'build-android.yml'} )
      if exists $texts{'build-android.yml'};

    # Crash evidence must not collect process memory or argument values.
    # Keep the opt-in debugger command file to this code-location allowlist.
    push @errors, windows_crash_command_errors( $texts{'build-windows.yml'} )
      if exists $texts{'build-windows.yml'};
    push @errors, windows_startup_command_errors( $texts{'build-windows.yml'} )
      if exists $texts{'build-windows.yml'};
    push @errors, windows_startup_probe_errors( $texts{'build-windows.yml'} )
      if exists $texts{'build-windows.yml'};
    push @errors,
      windows_process_wait_errors(
        $texts{'build-windows.yml'},
        'diagnostic',
        q!if (-not $debugger.WaitForExit(20000)) {!,
        q!if (-not $debugger.WaitForExit(120000)) {!,
        q!if (-not $flagProcess.WaitForExit((Remaining-ProbeMilliseconds))) {!,
        q!if (-not $debugger.WaitForExit((Remaining-ProbeMilliseconds))) {!,
q!if (-not $process.WaitForExit(5000)) { throw 'startup probe cleanup deadline' }!
      ) if exists $texts{'build-windows.yml'};

    if (@errors) {
        print STDERR "$_\n" for @errors;
        exit 1;
    }

    print "workflow-static: $checked workflow(s) and "
      . scalar(@action_files)
      . " action file(s) obey the manual-only trigger policy and fail on empty CTest selections\n";
    exit 0;
}

sub trigger_and_ctest_errors {
    my ( $file, $lines ) = @_;
    my @violations;
    my $in_on   = 0;
    my $seen_on = 0;
    my @triggers;
    for my $line ( @{$lines} ) {
        next if $line =~ /^\s*[#]/x;
        if ( $line =~ /^on:\s*$/x ) {
            $in_on   = 1;
            $seen_on = 1;
            next;
        }
        if ( $line =~ /^on:\s*(\S.*)$/x ) {
            $seen_on = 1;
            my $inline = $1;
            $inline =~ s/^\s*\[//x;
            $inline =~ s/\]\s*$//x;
            push @triggers, grep { /\S/x } split /\s*,\s*/x, $inline;
            $in_on = 0;
            next;
        }
        if ($in_on) {
            if ( $line =~ /^\S/x ) {
                $in_on = 0;
                next;
            }
            my ($trigger) = $line =~ / ^ \s{2} ([A-Za-z0-9_-]+) : /x;
            push @triggers, $trigger if defined $trigger;
            next;
        }
        if ( $line =~ /^\s*continue-on-error\s*:/x ) {
            push @violations, "$file: continue-on-error is forbidden";
        }

        # Only executing invocations are constrained. The two exclusions
        # are query-only forms: -N lists tests without running them and
        # --print-labels only reports the label inventory.
        if (   $line =~ /(?:^|\s)ctest\s+--/x
            && $line !~ /\s-N(?:\s|$)/x
            && $line !~ /--print-labels/x )
        {
            push @violations, "$file: ctest invocation lacks --no-tests=error"
              unless $line =~ /--no-tests=error/x;
            push @violations, "$file: ctest invocation lacks --output-junit"
              unless $line =~ /--output-junit/x;
        }
    }

    if ( !$seen_on ) {
        push @violations, "$file: missing on: block";
    }
    else {
        for my $trigger (@triggers) {
            $trigger =~ s/:$//x;
            next if $trigger eq 'workflow_dispatch';
            push @violations, "$file: unsupported trigger '$trigger'";
        }
        push @violations, "$file: workflow_dispatch is not declared"
          unless grep { $_ eq 'workflow_dispatch' } @triggers;
    }
    return @violations;
}

sub workflow_perl_errors {
    my ( $file, $text ) = @_;
    my @violations;
    ( my $without_comments = $text ) =~ s/^[ \t]*[#].*$//mgx;
    push @violations,
      "$file: interpreter policy requires Perl, not an alternate runtime"
      if $without_comments =~
      /\b(?:python[0-9.]*|pypy[0-9.]*|setup-python)\b/ix;
    if ( $file eq 'agent-maintenance.yml' || $file eq 'validation.yml' ) {
        my @commands = (
            'prove -v tests/agent_knowledge_validator/test_validator.t',
            'perl tools/scripts/check-shell-policy.pl'
        );
        push @commands,
          'perl scripts/validate-agent-knowledge.pl --scope worktree';
        for my $command (@commands) {
            push @violations, "$file: Perl knowledge contract missing: $command"
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
            push @violations,
              "$file: NUL-delimited Perl inventory missing: $command"
              if index( $without_comments, $command ) < 0;
        }
    }
    return @violations;
}

# OpenBSD steps can also begin with a uses-only YAML item.
sub openbsd_step {
    my ( $text, $name ) = @_;
    my $item = qr/ ^ [ ]{6} - [ ] /mx;
    my ($block) = $text =~ /
        ($item name: [ ] \Q$name\E \n .*?)
        (?= $item (?:name|uses): | \z)
    /sx;
    return $block;
}

# These callers intentionally accept both spaces and tabs before a step.
sub indented_named_step {
    my ( $text, $name ) = @_;
    my $heading = qr/ ^ [ \t]* - [ ] name: /mx;
    my ($block) = $text =~ /
        ($heading [ ] \Q$name\E \n .*?)
        (?= $heading | \z)
    /sx;
    return $block;
}

sub openbsd_copyback_errors {
    my ($text) = @_;
    my @violations;
    my $file = 'build-openbsd.yml';
    ( my $without_comments = $text ) =~ s/^[ \t]*[#].*$//mgx;

    # The pinned action throws before its final copyback when its
    # run input fails. Its named rsync shell instead pulls after the
    # command and returns the original command status.
    my $prepare = openbsd_step( $without_comments, 'Prepare OpenBSD VM' );
    my $build   = openbsd_step( $without_comments, 'Build inside OpenBSD' );
    my $upload  = openbsd_step( $without_comments, 'Upload OpenBSD test logs' );
    for my $required (
        'uses: vmactions/openbsd-vm@',
        'copyback: true',
        'sync: rsync',
        'custom-shell-name: openbsd'
      )
    {
        push @violations,
          "$file: OpenBSD failure copyback missing: prepare $required"
          if !defined $prepare || index( $prepare, $required ) < 0;
    }
    push @violations,
      "$file: OpenBSD failure copyback forbids the action run input"
      if defined $prepare && $prepare =~ /^[ ]{10}run:/mx;
    for my $required (
        'shell: openbsd {0}',
        'id: build_openbsd',
        'run: |',
        'set -eu',
        'cmake --build build-output',
        'build_status=$?',
        'exit "$build_status"'
      )
    {
        push @violations,
          "$file: OpenBSD failure copyback missing: build $required"
          if !defined $build || index( $build, $required ) < 0;
    }
    my @openbsd_steps = (
        [
            'Run OpenBSD test suite',        'build_openbsd',
            'ctest --test-dir build-output', 'ctest-openbsd.xml'
        ],
        [
            'Provision OpenBSD credential fixture account',
            'build_openbsd',
            'id: provision_credentials',
            'useradd -d /var/empty'
        ],
        [
            'Run OpenBSD non-root credential checks',
            'provision_credentials',
            'su -s /bin/sh openi2pd-ci',
            'ctest-openbsd-credentials.xml'
        ],
        [
            'Run OpenBSD privileged credential drop check',
            'provision_credentials',
            '--verify-privileged-drop',
            'ctest-openbsd-credential-drop.xml'
        ],
        [
            'Build with the OpenBSD POSIX Make front end',
            'build_openbsd',
            'make BUILD_DIR=build-make',
            'test -x build-make/i2pd'
        ]
    );
    for my $step_contract (@openbsd_steps) {
        my ( $name, $dependency, @required ) = @{$step_contract};
        my $step = openbsd_step( $without_comments, $name );
        my $condition =
            'if: ${{ !cancelled() && steps.'
          . $dependency
          . ".outcome == 'success' }}";
        for my $required ( $condition, 'shell: openbsd {0}', @required ) {
            push @violations,
              "$file: OpenBSD failure copyback missing: $name / $required"
              if !defined $step || index( $step, $required ) < 0;
        }
    }
    for my $required (
        'if: always()',                  'uses: actions/upload-artifact@',
        'build-output/Testing',          'configure.log',
        'build.log',                     'ctest-openbsd.xml',
        'ctest-openbsd-credentials.xml', 'ctest-openbsd-credential-drop.xml'
      )
    {
        push @violations,
          "$file: OpenBSD failure copyback missing: upload $required"
          if !defined $upload || index( $upload, $required ) < 0;
    }
    return @violations;
}

sub undeclared_input_errors {
    my ( $file, $text, $lines ) = @_;
    my @violations;

    # Every `inputs.NAME` reference must be declared by this workflow's own
    # `workflow_dispatch` inputs. GitHub evaluates an undeclared input as an
    # empty value instead of failing the dispatch, so a stale reference (the
    # removed `source_docs` gate is the historical example) would silently
    # disable a job or run it unconditionally.
    my %declared = map { $_ => 1 } declared_dispatch_inputs($lines);
    ( my $without_comments = $text ) =~ s/^[ \t]*[#].*$//mgx;
    my %referenced;
    while ( $without_comments =~ /\binputs\.([A-Za-z_][A-Za-z0-9_-]*)/gx ) {
        $referenced{$1} = 1;
    }
    for my $name ( sort keys %referenced ) {
        push @violations,
          "$file: inputs.$name is not declared under workflow_dispatch"
          unless $declared{$name};
    }
    return @violations;
}

# Discover every job id so the split-ownership and opt-in-gate checks work
# on real `jobs:` keys rather than any two-space mapping key elsewhere in
# the file (`permissions:` children are the common false positive).
sub job_blocks {
    my ($lines) = @_;
    my ( %blocks, $current, $in_jobs );
    for my $line (@$lines) {
        if ( $line =~ /^jobs:\s*$/x ) {
            $in_jobs = 1;
            next;
        }
        next unless $in_jobs;
        if ( $line =~ /^\S/x ) {
            $in_jobs = 0;
            $current = undef;
            next;
        }
        if ( $line =~ /^[ ]{2}([A-Za-z0-9_-]+):\s*$/x ) {
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
        next if $line =~ /^\s*[#]/x;
        if ( !$in_inputs ) {
            $in_inputs = 1 if $line =~ /^[ ]{4}inputs:\s*$/x;
            next;
        }
        if ( $line =~ /^[ ]{6}([A-Za-z0-9_-]+):/x ) {
            push @names, $1;
            next;
        }
        next if $line =~ /^\s*$/x;
        last if $line =~ /^[ ]{0,5}\S/x;    # dedented out of the inputs map
    }
    return @names;
}

# Composite actions under .github/actions can execute ctest too, so the
# same empty-selection and JUnit contract must cover them; they have no
# trigger block to inspect.
sub action_files {
    my ($actions_dir) = @_;
    my @action_files;
    if ( -d $actions_dir ) {
        File::Find::find(
            {
                no_chdir => 1,
                wanted   => sub {
                    push @action_files, $_
                      if -f $_ && $_ =~ /\.ya?ml\z/x;
                },
            },
            $actions_dir
        );
    }
    return @action_files;
}

sub linux_build_action_errors {
    my ( $path, $text ) = @_;
    my @violations;
    ( my $joined = $text ) =~ s/\\\n\s*/ /gx;
    $joined =~ s/`\n\s*/ /gx;

    # A failed compiler must still leave stderr and probe evidence.
    # Inspect commands and their upload block separately so an unused
    # filename elsewhere cannot satisfy the artifact contract.
    my $active = $joined;
    $active =~ s/^[ \t]*[#].*$//mgx;
    for my $required (
        q{STRICT_WARNINGS: ${{ inputs.warnings_as_errors }}},
        q{case "$STRICT_WARNINGS" in},
        q{ON|OFF)},
        q{-DI2PD_WARNINGS_AS_ERRORS="$STRICT_WARNINGS"}
      )
    {
        push @violations, "$path: strict warnings chain missing: $required"
          if index( $active, $required ) < 0;
    }
    my $input_heading = qr/ ^ [ ]{2} warnings_as_errors: \n /mx;
    my $input_body    = qr/ (?:(?! ^ [ ]{2} [A-Za-z_]+ : ).)*? /msx;
    push @violations, "$path: strict warnings action default must be OFF"
      unless $active =~
      /$input_heading $input_body ^[ ]{4}default:[ ]['"]?OFF['"]?\s*$/msx;
    for my $phase (
        [ 'configure', qr/cmake\s+-S\s+build\s+-B\s+build-output/x ],
        [ 'build',     qr/cmake\s+--build\s+build-output/x ]
      )
    {
        my ( $name, $command ) = @{$phase};
        my $tee_stderr = qr/ [^\n]* 2>&1 \s* \| \s* tee \s+ /x;
        push @violations, "$path: build evidence missing: $name stderr capture"
          unless $active =~
          /$command $tee_stderr (?:"?\$PWD\/)?\Q$name\E[.]log"?(?:\s|$)/x;
    }
    my $build_block =
      indented_named_step( $active, 'Configure and build native targets' );
    push @violations,
      "$path: build evidence missing: pipeline failure propagation"
      unless defined $build_block
      && $build_block =~ /set[ ]-euo[ ]pipefail/x
      && $build_block =~ /^[ ]{6}id:[ ]native_build$/mx;
    my $step_heading = qr/ ^ [ ]{4} - [ ] name: /mx;
    for my $name (
        'Run all CTest targets',
        'Run I2PSnark compatibility regressions',
        'Run client parsing and persistence regressions',
        'Run profile, Android lifecycle and firewall regressions',
        'Verify CTest subsystem labels',
        'Validate Perl and static interoperability fixtures'
      )
    {
        my ($step) =
          $active =~
          /($step_heading [ ] \Q$name\E \n .*?)(?= $step_heading | \z)/sx;
        my $gate =
          q{if: ${{ !cancelled() && steps.native_build.outcome == 'success' }}};
        push @violations, "$path: independent native evidence missing: $name"
          if !defined $step || index( $step, $gate ) < 0;
    }
    my $heading       = qr/ ^ [ \t]* - [ ] name: /mx;
    my $step_body     = qr/ (?:(?! $heading ).) /sx;
    my $artifact_line = qr/ uses: [ ] actions\/upload-artifact [^\n]* \n /x;
    my ($upload_block) =
      $active =~ /($heading [^\n]*\n$step_body*? $artifact_line $step_body*)/sx;
    for my $required (
        'if: always()', 'configure.log',
        'build.log',    'build-output/CMakeFiles/CMakeConfigureLog.yaml'
      )
    {
        push @violations, "$path: build evidence missing: upload $required"
          if !defined $upload_block
          || index( $upload_block, $required ) < 0;
    }
    return @violations;
}

sub action_command_errors {
    my ( $path, $text ) = @_;
    my @violations;
    ( my $joined = $text ) =~ s/\\\n\s*/ /gx;
    $joined =~ s/`\n\s*/ /gx;
    for my $line ( split /\n/x, $joined ) {
        next if $line =~ /^\s*[#]/x;
        push @violations,
          "$path: interpreter policy requires Perl, not an alternate runtime"
          if $line =~ /\b(?:python[0-9.]*|pypy[0-9.]*|setup-python)\b/ix;
        if ( $line =~ /^\s*continue-on-error\s*:/x ) {
            push @violations, "$path: continue-on-error is forbidden";
        }
        if (   $line =~ /(?:^|\s)ctest\s+--/x
            && $line !~ /\s-N(?:\s|$)/x
            && $line !~ /--print-labels/x )
        {
            push @violations, "$path: ctest invocation lacks --no-tests=error"
              unless $line =~ /--no-tests=error/x;
            push @violations, "$path: ctest invocation lacks --output-junit"
              unless $line =~ /--output-junit/x;
        }
    }
    return @violations;
}

sub openssl_branch_errors {
    my ( $dir, $texts, $files ) = @_;
    my @violations;

    # Regular OpenSSL branch lanes consume bottles. A separate diagnostic
    # builds pinned crypto with TSan; it never replaces those matrix lanes.
    my %branch_formula = (
        'build-linux-openssl3.yml' => 'openssl@3',
        'build-linux-openssl4.yml' => 'openssl@4',
    );
    for my $file ( sort keys %branch_formula ) {
        my $path = File::Spec->catfile( $dir, $file );
        if ( !-f $path ) {
            push @violations, "$file: missing Linux OpenSSL branch workflow";
            next;
        }
        my $text    = $texts->{$file};
        my $formula = $branch_formula{$file};
        my $active  = $text;
        $active =~ s/^[ \t]*[#].*$//mgx;
        my $input_heading = qr/ ^ [ ]{6} warnings_as_errors: \n /mx;
        my $input_body    = qr/ (?:(?! ^ [ ]{6} [A-Za-z_]+ : ).)*? /msx;
        my $boolean_type  = qr/ ^ [ ]{8} type: [ ] boolean \n /mx;
        push @violations,
          "$file: strict warnings dispatch must be boolean default false"
          unless $active =~
/$input_heading $input_body $boolean_type $input_body^[ ]{8}default:[ ]false\s*$/msx;
        push @violations,
          "$file: strict warnings forwarding missing"
          if index(
            $active,
q{warnings_as_errors: ${{ inputs.warnings_as_errors && 'ON' || 'OFF' }}}
          ) < 0;

        if ( index( $text, $formula ) < 0 ) {
            push @violations,
              "$file: does not select the $formula Homebrew bottle";
        }
        if ( $text =~ /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/x ) {
            push @violations,
"$file: must use the Homebrew $formula bottle, not a source build";
        }
    }
    for my $file ( @{$files} ) {
        next if exists $branch_formula{$file};
        if ( $texts->{$file} =~
            /openssl\.org\/source|OPENSSL_SHA256|OPENSSL_PIN/x )
        {
            push @violations,
"$file: OpenSSL source builds moved to the Homebrew branch workflows";
        }
    }
    return @violations;
}

sub crypto_tsan_errors {
    my ( $root, $actions_dir, $texts ) = @_;
    my @violations;
    my $crypto_diagnostic = $texts->{'crypto-tsan.yml'};
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
            push @violations,
              "crypto-tsan.yml: missing instrumented crypto boundary $required"
              if index( $crypto_diagnostic . $crypto_action, $required ) < 0;
        }
        push @violations,
          'crypto-tsan.yml: sanitizer suppressions are forbidden'
          if ( $crypto_diagnostic . $crypto_action ) =~
          /suppressions=|ignore_noninstrumented_modules|halt_on_error=0/x;
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
        push @violations,
          "validation.yml: missing shared TSan crypto boundary $required"
          if index( $texts->{'validation.yml'}, $required ) < 0;
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
                push @violations,
                  "crypto-tsan.yml: incomplete upstream macro patch $required"
                  if index( $patch, $required ) < 0;
            }
        }
        else {
            push @violations, 'crypto-tsan.yml: missing upstream macro patch';
        }
    }
    return @violations;
}

sub split_job_errors {
    my ( $dir, $files, $blocks_for ) = @_;
    my @violations;

    # The manual lanes are split by concern: Linux builds and packaging
    # checks, Android, and the validation lanes (Doxygen, compile-free agent
    # checks, Perl quality, sanitizers and fuzzing). A job must live in its
    # owning workflow, and the former aggregate names must not return.
    my %split_jobs = (
        'build-linux.yml' =>
          [qw(install-components interop system-openssl-floor)],
        'build-android.yml' => [qw(android android-ui android-sign)],
        'validation.yml'    =>
          [qw(source-docs skills-static perl-quality sanitizers fuzz-smoke)],
    );
    my %job_owner;
    for my $file ( sort keys %split_jobs ) {
        if ( !exists $blocks_for->{$file} ) {
            push @violations, "$file: missing split workflow";
            next;
        }
        for my $job ( @{ $split_jobs{$file} } ) {
            if ( !exists $blocks_for->{$file}{$job} ) {
                push @violations, "$file: missing job '$job'";
            }
            $job_owner{$job} = $file;
        }
    }

    # Native compile failures must not hide the timer API policy itself.
    my $timer_guard_job = $blocks_for->{'validation.yml'}{'skills-static'}
      // '';
    my @timer_guard_lines = split /\n/x, $timer_guard_job;
    for my $command (
        q{cmake -DPROJECT_ROOT="$PWD" -P tests/check-timer-cancellation.cmake},
q{cmake -DPROJECT_ROOT="$PWD" -DBINARY_DIR="$RUNNER_TEMP/timer-contract" -P tests/test-timer-cancellation-contract.cmake}
      )
    {
        push @violations,
          'validation.yml: missing independent timer API contract'
          unless
          grep { my $line = $_; $line =~ s/\A\s+|\s+\z//gx; $line eq $command }
          @timer_guard_lines;
    }
    push @violations,
      'validation.yml: timer API contract must not wait on builds'
      if $timer_guard_job =~ /^\s+needs:/mx;

    for my $legacy (qw(build.yml fuzz.yml)) {
        if ( -f File::Spec->catfile( $dir, $legacy ) ) {
            push @violations,
              "$legacy: the aggregate workflow was split by concern";
        }
    }
    for my $job ( sort keys %job_owner ) {
        for my $file ( @{$files} ) {
            next if $file eq $job_owner{$job};
            if ( exists $blocks_for->{$file}{$job} ) {
                push @violations,
                  "$file: job '$job' belongs to $job_owner{$job}";
            }
        }
    }
    return @violations;
}

sub sanitizer_evidence_errors {
    my ($sanitizers) = @_;
    my @violations;
    my $active = $sanitizers;
    $active =~ s/^[ \t]*[#].*$//mgx;
    $active =~ s/\\\n\s*/ /gx;
    for my $phase (
        [ 'configure', qr/cmake\s+-S\s+build\s+-B\s+build-sanitizers/x ],
        [ 'build',     qr/cmake\s+--build\s+build-sanitizers/x ]
      )
    {
        my ( $name, $command ) = @{$phase};
        my $tee_stderr = qr/ [^\n]* 2>&1 \s* \| \s* tee \s+ /x;
        push @violations,
"validation.yml: sanitizer build evidence missing: $name stderr capture"
          unless $active =~
          /$command $tee_stderr sanitizer-\Q$name\E[.]log(?:\s|$)/x;
    }
    my $build_block =
      indented_named_step( $active, 'Build and run sanitizer CTest matrix' );
    push @violations,
"validation.yml: sanitizer build evidence missing: pipeline failure propagation"
      unless defined $build_block && $build_block =~ /set[ ]-euo[ ]pipefail/x;
    my $upload_block =
      indented_named_step( $active, 'Upload sanitizer test logs' );
    for my $required (
        'if: always()',
        'uses: actions/upload-artifact',
        'sanitizer-configure.log',
        'sanitizer-build.log',
        'build-sanitizers/CMakeCache.txt',
        'build-sanitizers/CMakeFiles/CMakeConfigureLog.yaml'
      )
    {
        push @violations,
          "validation.yml: sanitizer build evidence missing: upload $required"
          if !defined $upload_block
          || index( $upload_block, $required ) < 0;
    }
    for my $image (
        qw(test-eddsa test-i2ptunnel-stop test-i2ptunnel-targets
        test-eciesx25519-ratchet-session test-runnableservice-lifecycle
        test-i2cp-session-lifecycle)
      )
    {
        if ( index( $sanitizers, "build-sanitizers/tests/$image" ) < 0 ) {
            push @violations,
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
            push @violations,
              "validation.yml: sanitizer identity evidence missing: $marker";
        }
    }
    return @violations;
}

sub opt_in_errors {
    my ($blocks_for) = @_;
    my @violations;

    # The expensive opt-in lanes stay opt-in: an Android API 29 emulator boot or
    # a Perl::Critic run on every dispatch is a cost regression, and an
    # accidental `if:` removal is otherwise invisible until the job starts.
    my %input_gate = (
        'build-linux.yml' =>
          { 'system-openssl-floor' => 'linux_floor_toolchains' },
        'build-android.yml' =>
          { 'android-ui' => 'android_ui', 'android-sign' => 'sign_release' },
        'validation.yml' => { 'perl-quality' => 'perl_style' },
    );
    for my $file ( sort keys %input_gate ) {
        next unless exists $blocks_for->{$file};
        for my $job ( sort keys %{ $input_gate{$file} } ) {
            my $input = $input_gate{$file}{$job};
            if ( !exists $blocks_for->{$file}{$job} ) {
                push @violations, "$file: missing opt-in job '$job'";
                next;
            }
            if ( $blocks_for->{$file}{$job} !~ /\binputs\.\Q$input\E\b/x ) {
                push @violations,
                  "$file: job '$job' must stay gated on inputs.$input";
            }
        }
    }
    return @violations;
}

# Step bodies end at the next item at the Android job's step indentation.
sub android_step_body {
    my ( $job, $step_name ) = @_;
    my $step_item = qr/ ^ [ ]{6} - [ ] /mx;
    my ($body) = $job =~ / $step_item name: [ ] \Q$step_name\E \n
        (.*?) (?= $step_item | \z ) /sx;
    return $body;
}

sub android_ui_errors {
    my ($ui) = @_;
    my @violations;
    my $acceleration =
      android_step_body( $ui, 'Verify KVM access for the Android UI runner' );
    push @violations,
'build-android.yml: Android UI must verify runner-only KVM access before building'
      if !defined($acceleration)
      || index( $ui, 'Verify KVM access' ) >
      index( $ui, 'Build Android x86_64' )
      || index( $acceleration, q{sudo chown -- "$UID" /dev/kvm} ) < 0
      || index( $acceleration, q{sudo chmod u+rw /dev/kvm} ) < 0
      || index(
        $acceleration,
q{timeout --kill-after=5s 15s "$ANDROID_HOME/emulator/emulator" -accel-check}
      ) < 0
      || index( $acceleration,
        q{grep -Fxq '0' "$RUNNER_TEMP/emulator-acceleration.txt"} ) < 0;
    my $boot = android_step_body( $ui, 'Boot API 29 emulator' );
    push @violations,
      'build-android.yml: Android emulator startup must check process liveness'
      if !defined($boot)
      || index( $boot, q{emulator_pid=$!} ) < 0
      || index( $boot, q{kill -0 "$emulator_pid"} ) < 0
      || index( $boot, q{-s emulator-5554 get-state} ) < 0
      || index( $boot, q{-s emulator-5554 shell getprop sys.boot_completed} ) <
      0;
    push @violations,
'build-android.yml: Android AVD producer and emulator must share a verified lookup directory'
      if !defined($boot)
      || index( $boot, q{export ANDROID_AVD_HOME="$RUNNER_TEMP/openi2pd-avd"} )
      < 0
      || index( $boot, q{--path "$ANDROID_AVD_HOME/openi2pd-api29.avd"} ) < 0
      || index( $boot, q{"$emulator" -list-avds} ) < 0
      || index( $boot, q{grep -Fxq 'openi2pd-api29'} ) < 0;
    push @violations, android_instrumentation_errors($ui);
    return @violations;
}

sub android_instrumentation_errors {
    my ($ui) = @_;
    my @violations;
    my $diagnostics =
      android_step_body( $ui, 'Collect emulator diagnostics on failure' );
    push @violations,
      'build-android.yml: Android emulator diagnostics must be bounded'
      if !defined($diagnostics)
      || $diagnostics !~ /^[ ]{8}timeout-minutes:[ ]1$/mx
      || index( $diagnostics,
        q{timeout --kill-after=5s 15s "$ADB" -s emulator-5554 logcat} ) < 0;
    my $instrumentation = android_step_body( $ui,
        'Run UI and accessibility instrumentation tests' );
    push @violations,
'build-android.yml: Android instrumentation needs a deadline before diagnostics'
      if !defined($instrumentation)
      || $instrumentation !~ /^[ ]{8}timeout-minutes:[ ]32$/mx
      || index(
        $instrumentation,
q{timeout --kill-after=15s 30m ./gradlew :i2pd:connectedDebugAndroidTest}
      ) < 0;

    for my $report (
        'emulator.log',              'logcat.txt',
        'emulator-acceleration.txt', 'instrumentation-state.txt'
      )
    {
        push @violations,
"build-android.yml: missing Android emulator diagnostic artifact $report"
          if index( $ui, q{${{ runner.temp }}/} . $report ) < 0;
    }
    return @violations;
}

sub android_signing_errors {
    my ($android) = @_;
    my @violations;

    # Only the isolated, explicitly approved job can access signing secrets.
    my $sign = $android->{'android-sign'} // '';
    for my $marker (
        q{environment: android-openi2pd-release},
        q{github.ref == 'refs/heads/main'},
        q{needs: android},
        q{permissions: {}},
        q{artifact-ids: ${{ needs.android.outputs.unsigned-apk-id }}},
        q{Signer #1 certificate SHA-256 digest: $EXPECTED_CERT}
      )
    {
        push @violations, "build-android.yml: signing boundary lacks $marker"
          if index( $sign, $marker ) < 0;
    }
    push @violations, 'build-android.yml: signer must not run repository code'
      if $sign =~ /actions\/checkout|gradlew|uses:\s*\.\//x;
    my $package_check =
q{grep -Eq "^package: name='dev[.]uhden[.]i2pd' versionCode='[0-9]+'" <<<"$badging"};
    for my $step ( 'Inspect and align production APK',
        'Verify production certificate and APK integrity' )
    {
        my $body = android_step_body( $sign, $step );
        push @violations,
          "build-android.yml: exact APK package check missing in $step"
          if !defined($body) || index( $body, $package_check ) < 0;
    }
    my $secrets_namespace   = 'secrets.OPENI2PD_';
    my $release_environment = 'android-openi2pd-release';
    for my $job ( keys %{$android} ) {
        next if $job eq 'android-sign';
        push @violations, "build-android.yml: signing credentials in $job"
          if $android->{$job} =~
/\Q$release_environment\E | \Q$secrets_namespace\E (?:KEYSTORE_|KEY_PASSWORD)/x;
    }
    return @violations;
}

sub android_native_errors {
    my ( $root, $blocks_for ) = @_;
    my @violations;

    # Dependency triplets and Android ABIs use separate naming schemes.
    # Check both prefixes and the overlay so a native build cannot resolve a
    # different architecture or API floor before Gradle starts.
    my %android_native = (
        android      => [ 'arm64-android', 'arm64-v8a', 'build-android' ],
        'android-ui' => [ 'x64-android',   'x86_64', 'build-android-x86_64' ],
    );
    for my $job ( sort keys %android_native ) {
        if ( !exists $blocks_for->{'build-android.yml'}{$job} ) {
            push @violations,
              "build-android.yml: missing Android dependency job '$job'";
            next;
        }
        my ( $triplet, $abi, $build_directory ) = @{ $android_native{$job} };
        my $active = $blocks_for->{'build-android.yml'}{$job};
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
            push @violations,
"build-android.yml: Android dependency boundary in $job lacks $required"
              if index( $active, $required ) < 0;
        }
        while ( $active =~ /-DVCPKG_TARGET_TRIPLET=([^\s\\]+)/gx ) {
            push @violations,
"build-android.yml: Android dependency boundary has wrong triplet in $job"
              if $1 ne $triplet;
        }
        my $path = File::Spec->catfile( $root, 'contrib', 'ci', 'android',
            "$triplet.cmake" );
        if ( open my $overlay, '<', $path ) {
            my $text = do { local $/ = undef; <$overlay> };
            close $overlay
              or push @violations,
"build-android.yml: Android dependency boundary cannot close $path";
            $text =~ s/^[ \t]*\#[^\n]*$//mgx;
            my $include = index( $text,
                    q{include("$ENV{VCPKG_ROOT}/triplets/}
                  . $triplet
                  . q{.cmake")} );
            my $floor = index( $text, 'set(VCPKG_CMAKE_SYSTEM_VERSION 29)' );
            my $build_type = index( $text, 'set(VCPKG_BUILD_TYPE release)' );
            push @violations,
"build-android.yml: Android dependency boundary has invalid overlay $triplet"
              if $include < 0
              || $floor <= $include
              || $build_type <= $include;
        }
        else {
            push @violations,
"build-android.yml: Android dependency boundary lacks overlay $triplet";
        }
    }
    return @violations;
}

sub android_java_probe_errors {
    my ($text) = @_;
    my @violations;

    # The Java 25 probe must consume the same R8 dependency as AGP.
    my $probe_heading = '- name: Probe Java 25 D8/R8 dexing';
    my ($probe) =
      $text =~ /[ ]{6}\Q$probe_heading\E\n(.*?)(?=[ ]{6}-[ ]name:|\z)/sx;
    for my $required (
'(cd android && ./gradlew --no-daemon --warning-mode all writeJava25R8Classpath)',
        'R8_JAR=$(cat android/build/reports/java25/r8-classpath.txt)',
        'test -f "$R8_JAR"'
      )
    {
        push @violations,
          'build-android.yml: Java 25 probe lacks AGP-resolved R8'
          if !defined($probe) || index( $probe, $required ) < 0;
    }
    push @violations,
      'build-android.yml: Java 25 probe must not scan caches or SDKs'
      if defined($probe) && $probe =~ /\bfind\s/x;
    return @violations;
}

# Inventory complete call lines rather than sharing a spelling across
# processes or allowing a second call to satisfy a missing deadline.
sub windows_process_wait_errors {
    my ( $source, $context, @expected ) = @_;
    my @violations;
    my %wait_roles = map { $_ => 0 } @expected;
    for my $wait_line ( $source =~ /^([^\n]*WaitForExit[^\n]*)$/mgx ) {
        $wait_line =~ s/\A\s+|\s+\z//gx;
        if ( exists $wait_roles{$wait_line} ) {
            ++$wait_roles{$wait_line};
        }
        else {
            push @violations,
              "build-windows.yml: $context has an unreviewed process wait";
        }
    }
    push @violations,
      "build-windows.yml: $context process wait inventory changed"
      if grep { $_ != 1 } values %wait_roles;
    return @violations;
}

sub windows_startup_command_errors {
    my ($windows) = @_;
    my @violations;
    my $step_title = '- name: Capture bounded router loader and entry evidence';
    my ($step) =
      $windows =~ /([ ]{6}\Q$step_title\E\n.*?)(?=[ ]{6}-[ ]name:)/sx;
    return 'build-windows.yml: missing startup diagnostic step'
      unless defined $step;
    my $write_commands = q{| Set-Content -Encoding ascii $commands};
    my ($commands) =
      $step =~ /[ ]{10}\@\(\n(.*?)[ ]{10}\) [ ] \Q$write_commands\E/sx;
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
      ? map { /^\s*'([^']*)'\s*,?\s*$/x ? $1 : 'INVALID' }
      split /\n/x, $commands
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
      if index( $windows,
        q{if: ${{ always() && steps.msys2_build.outcome == 'success' }}} ) < 0;
    push @violations, 'build-windows.yml: missing startup diagnostic artifact'
      unless $windows =~ /^\s*build-output\/startup-debugger\s*$/mx;
    return @violations;
}

sub windows_crash_command_errors {
    my ($windows) = @_;
    my @violations;
    my $debug_arguments =
q{$debugArguments = @('-G', '-noshell', '-lines', '-y', "`"$symbols`"", '-logo', "`"$log`"", '-cf', "`"$commands`"", "`"$binary`"")};
    my $supervisor_test  = 'test-privsep-windows-supervisor';
    my $supervisor_image = "build-msvc/tests/Release/$supervisor_test.exe";
    push @violations,
      'build-windows.yml: missing bounded worker initialization diagnostic'
      if $windows !~ /^ \s* \Q$debug_arguments\E \s* $/mx
      || $windows !~ / foreach [ ] \(\$test [ ] in [ ] \@\(
                           [^)]* '\Q$supervisor_test\E' \s* \)\) /x
      || $windows !~ /^ \s* \Q$supervisor_image\E \s* $/mx
      || index( $windows, q{$debugger.WaitForExit(120000)} ) < 0;
    push @violations,
      'build-windows.yml: crash marker must fail the diagnostic step'
      if index( $windows,
        q{Select-String -Path $log -Pattern '^OPENI2PD_CRASH\s*$' -Quiet} ) < 0
      || index( $windows, q{$failures += "$test debugger exception"} ) < 0
      || index( $windows, q{$failures += "$test missing debugger log"} ) < 0;
    push @violations, windows_exception_command_errors($windows);
    return @violations;
}

sub windows_exception_command_lines {
    my ($windows) = @_;
    my @commands;
    my $inside = 0;
    for my $line ( split /\n/x, $windows ) {
        $line =~ s/\A\s+|\s+\z//gx;
        if ( !$inside ) {
            $inside = 1 if $line eq q{@'};
            next;
        }
        return ( 1, @commands )
          if $line eq q{'@ | Set-Content -Encoding ascii $commands};
        push @commands, $line;
    }
    return ( 0, @commands );
}

# Validate each supplied exception-command block independently.
sub windows_exception_command_errors {
    my ( $windows, $probe_mode ) = @_;
    my @violations;
    my ( $closed, @lines ) = windows_exception_command_lines($windows);
    my %events = map { $_ => 1 } qw(av 0xc0000374 0xc0000409 0xc0000142);
    my %seen;

    # Only the nonce-free probe may inspect termination status. Exact
    # lines keep nested breakpoint commands outside the heap allowlist.
    my %probe_commands = map { $_ => 1 } (
q{sxe -c "bu ntdll!NtTerminateProcess \".if (@edx == 0xc0000142) { .echo OPENI2PD_PROBE_TERMINATION; ln @rip; ln @$ra; ub @$ra L20; kc 40; }; gc\"; g" cpr},
        q{sxn -c ".echo OPENI2PD_PROBE_EXIT; .lastevent" epr},
q{sxe -c ".echo OPENI2PD_PROBE_KERNELBASE; .reload /f kernelbase.dll; lmvm kernelbase; g" ld:kernelbase.dll}
    );
    my %probe_seen;
    my %allowed = map { $_ => 1 }
      ( '.echo OPENI2PD_CRASH', '.lastevent', 'ln @rip', 'kc 40', 'q' );
    for my $line (@lines) {
        next if !length $line || $line eq 'g';
        if (
            $probe_mode
            && (   $line eq '.echo OPENI2PD_PROBE_ARMED'
                || $line eq 'lmvm ntdll' )
          )
        {
            $probe_seen{$line}++;
            next;
        }
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
      unless $closed && keys(%seen) == keys(%events);
    push @violations,
      'build-windows.yml: startup probe termination evidence missing'
      if $probe_mode
      && ( keys(%probe_seen) != 5 || grep { $_ != 1 } values %probe_seen );
    return @violations;
}

sub windows_startup_probe_errors {
    my ($windows) = @_;
    my @violations;
    my $step_heading = '- name: Capture contained worker initialization';
    my ($probe) =
      $windows =~ /[ ]{6}\Q$step_heading\E\n(.*?)(?=[ ]{6}-[ ]name:|\z)/sx;
    return 'build-windows.yml: missing bounded startup probe'
      unless defined $probe;
    my $write_here_string = q{'@ | Set-Content -Encoding ascii $commands};
    my @command_files     = $windows =~ /^\s*\Q$write_here_string\E\s*$/mgx;
    my @debugger_launches = $windows =~ /Start-Process[ ]-FilePath[ ]\$cdb\b/gx;
    push @violations,
      'build-windows.yml: startup probe command-file inventory changed'
      unless @command_files == 2 && @debugger_launches == 3;
    my $arguments =
q{$probeArguments = @('-o', '-noshell', '-lines', '-y', "`"$symbols`"", '-logo', "`"$log`"", '-cf', "`"$commands`"", "`"$binary`"", '--diagnose-worker-initialization')};
    push @violations,
'build-windows.yml: startup probe must trace only the closed diagnostic mode'
      if index( $probe, $arguments ) < 0;
    my @child_lines = $windows =~ /^\s*(\$\w+[ ]=[ ]\@\([^\n]*'-o'[^\n]*)$/mgx;
    push @violations,
      'build-windows.yml: startup probe cannot trace authenticated workers'
      unless @child_lines == 1 && $child_lines[0] eq $arguments;

    # Each process owns a separate wait. A shared substring can remain
    # present after another receiver loses its deadline. Accept only the
    # closed source grammar below, once per role, including cleanup.
    push @violations,
      windows_process_wait_errors(
        $probe,
        'startup probe',
        q!if (-not $flagProcess.WaitForExit((Remaining-ProbeMilliseconds))) {!,
        q!if (-not $debugger.WaitForExit((Remaining-ProbeMilliseconds))) {!,
q!if (-not $process.WaitForExit(5000)) { throw 'startup probe cleanup deadline' }!
      );

    for my $required (
        q{if: ${{ always() && steps.msvc_tests.outcome == 'failure' }}},
        q{Get-FileHash -Algorithm SHA256 -Path $binary},
        q{$symbolCache = Join-Path $env:RUNNER_TEMP 'openi2pd-os-symbols'},
q{$symbols = (Split-Path -Parent $binary) + ';srv*' + $symbolCache + '*https://msdl.microsoft.com/download/symbols'},
        q{Select-String -Path $log -Pattern '^OPENI2PD_PROBE_ARMED\s*$' -Quiet},
        q{60000 - $probeClock.ElapsedMilliseconds},
        q{@('/i', $image, '+sls')},
        q{$restoreKey.SetValue('GlobalFlag', $savedFlag, $savedKind)},
        q{$restoreKey.DeleteValue('GlobalFlag', $false)},
        q{$savedKind = $key.GetValueKind('GlobalFlag')},
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
    my $child_result =
      index( $probe, q{throw ('contained startup probe child exited '} );
    my $debugger_failure = index( $probe, q{if ($debugger.ExitCode -ne 0)} );
    push @violations,
'build-windows.yml: startup probe must classify a child result before the debugger exit'
      if $child_result < 0 || $debugger_failure < $child_result;
    push @violations, 'build-windows.yml: startup probe lacks retained artifact'
      unless $windows =~ /^\s*build-msvc\/startup-probe\s*$/mx;
    push @violations, windows_exception_command_errors( $probe, 1 );
    return @violations;
}

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
F<build-linux.yml> (C<install-components>, C<interop>,
C<system-openssl-floor>), F<build-android.yml>
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
