#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: black-box Agent Knowledge Validation contract regressions.
# Fixtures and commits exist only in private temporary repositories.
# Prepared for manual CI; do not execute this suite during agent work.

use strict;
use warnings;
use 5.016;
use utf8;

use Cwd qw(abs_path getcwd);
use File::Basename qw(dirname);
use File::Copy qw(copy);
use File::Find ();
use File::Path qw(make_path);
use File::Spec;
use File::Temp qw(tempfile);
use FindBin qw($Bin);
use IPC::Open3 qw(open3);
use Encode ();
use Errno qw(EINTR);
use JSON::PP;
use Test::More;

sub fs_text {
    my ($value) = @_;
    return utf8::is_utf8($value)
      ? $value : Encode::decode( 'UTF-8', $value, Encode::FB_CROAK() );
}

sub fs_native {
    my ($value) = @_;
    return $^O eq 'MSWin32' || !utf8::is_utf8($value)
      ? $value : Encode::encode( 'UTF-8', $value );
}

# Native Win32 builds can expose symlink as a throwing stub rather than
# return false. Probe only this operation; retain the other path cases.
sub create_fixture_symlink {
    my ($target, $link) = @_;
    my $linked = eval { symlink $target, $link };
    my $reason = $@ || "$!";
    # MSYS can report success after copying instead of linking. The
    # existing capability skip requires a real, readable symlink.
    if ($linked) {
        my $observed = -l $link ? readlink($link) : undef;
        if ( !defined $observed ) {
            $linked = 0;
            $reason = 'host did not create a readable symbolic link';
        }
    }
    die "create symlink fixture: $reason\n" if !$linked && $^O ne 'MSWin32';
    $reason =~ s/\s+/ /g;
    return ($linked, $reason);
}

my $source_root = fs_text( abs_path( File::Spec->catdir( $Bin, '..', '..' ) ) );
my $validator = File::Spec->catfile( $source_root, 'scripts',
    'validate-agent-knowledge.pl' );

my $agent_text = <<'AGENT';
# Repository guidance

## Continuous Agent & Skills Maintenance

Keep operational guidance accurate and reusable.

See [the readme](README.md).
AGENT

my $skill_text = <<'SKILL';
---
name: example
description: Guidance for the example subsystem.
---
# Example skill

## Maintenance & Continuous Learning

Keep the procedure current.
SKILL

{
    package ValidatorFixture;

    sub new {
        my ( $class, $root_name ) = @_;
        $root_name = 'repository fixture' unless defined $root_name;
        my $temporary = File::Temp->newdir(
            'agent-knowledge-contract-XXXXXX', TMPDIR => 1, CLEANUP => 1 );
        my $self = bless {
            temporary => $temporary,
            root      => File::Spec->catdir( main::fs_text("$temporary"), $root_name ),
        }, $class;
        File::Path::make_path( main::fs_native( $self->{root} ) );
        my $target = $self->path('scripts/validate-agent-knowledge.pl');
        File::Path::make_path( File::Basename::dirname($target) );
        File::Copy::copy( main::fs_native($validator), $target )
          or die "copy validator fixture: $!\n";

        # The port owns a core-only TOML module tree beside the CLI.
        # Copy its real files without following a source-tree symlink.
        my $library = File::Spec->catdir( $source_root, 'scripts', 'lib' );
        die "missing validator library tree\n"
          unless -d main::fs_native($library) && !-l main::fs_native($library);
        File::Find::find(
            {
                no_chdir => 1,
                wanted   => sub {
                    my $path = main::fs_text($File::Find::name);
                    die "symlink in validator library fixture\n" if -l main::fs_native($path);
                    return if -d main::fs_native($path);
                    die "nonregular validator library fixture\n" unless -f main::fs_native($path);
                    my $relative = File::Spec->abs2rel( $path, $library );
                    my $copy = File::Spec->catfile(
                        $self->{root}, 'scripts', 'lib', $relative );
                    File::Path::make_path( main::fs_native( File::Basename::dirname($copy) ) );
                    File::Copy::copy( main::fs_native($path), main::fs_native($copy) )
                      or die "copy validator library fixture: $!\n";
                },
            },
            main::fs_native($library)
        );
        $self->git( 'init', '-q', '-b', 'main' );
        $self->git( 'config', 'user.name', 'Validator Test' );
        $self->git( 'config', 'user.email', 'validator@example.invalid' );
        $self->git( 'config', 'commit.gpgsign', 'false' );
        $self->git( 'config', 'core.autocrlf', 'false' );
        my $hooks = File::Spec->catdir( "$temporary", 'empty-hooks' );
        File::Path::make_path($hooks);
        $self->git( 'config', 'core.hooksPath', $hooks );
        $self->write( 'AGENTS.md', $agent_text );
        $self->write( 'README.md', "Repository fixture.\n" );
        $self->write( 'src/example.cpp', "int value = 1;\n" );
        $self->write( '.agents/skills/example/SKILL.md', $skill_text );
        $self->commit('baseline');
        $self->{base} = $self->git( 'rev-parse', 'HEAD' );
        $self->{base} =~ s/\s+\z//;
        return $self;
    }

    sub path {
        my ( $self, $relative ) = @_;
        return main::fs_native(
            File::Spec->catfile( $self->{root}, split m{/}, main::fs_text($relative) ) );
    }

    sub write {
        my ( $self, $relative, $content ) = @_;
        my $path = $self->path($relative);
        File::Path::make_path( File::Basename::dirname($path) );
        open my $file, '>:raw:encoding(UTF-8)', $path
          or die "open fixture: $!\n";
        print {$file} $content or die "write fixture: $!\n";
        close $file or die "close fixture: $!\n";
        return;
    }

    sub read {
        my ( $self, $relative ) = @_;
        open my $file, '<:raw:encoding(UTF-8)', $self->path($relative)
          or die "open fixture: $!\n";
        local $/ = undef;
        my $text = <$file>;
        close $file or die "close fixture: $!\n";
        return defined $text ? $text : '';
    }

    sub run_command {
        my ( $self, $cwd, @command ) = @_;
        my ( $stdout, $stdout_path ) = File::Temp::tempfile(
            'stdout-XXXXXX', DIR => "$self->{temporary}", UNLINK => 1 );
        my ( $stderr, $stderr_path ) = File::Temp::tempfile(
            'stderr-XXXXXX', DIR => "$self->{temporary}", UNLINK => 1 );
        binmode $stdout;
        binmode $stderr;

        # Use two regular files rather than polling anonymous pipes:
        # Win32 select() cannot drain pipes, and a large diagnostic must
        # never deadlock its child. List-form Open3 never invokes a shell.
        my $previous = Cwd::getcwd();
        chdir main::fs_native($cwd) or die "enter fixture directory: $!\n";
        my ( $pid, $input, $spawn_error );
        {
            local %ENV = %ENV;
            delete @ENV{qw(GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_COMMON_DIR
                GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
                GIT_CONFIG_PARAMETERS GIT_CONFIG_COUNT GIT_CEILING_DIRECTORIES
                GIT_TEMPLATE_DIR PERL5LIB PERL5OPT)};
            $ENV{GIT_CONFIG_NOSYSTEM} = 1;
            $ENV{GIT_CONFIG_GLOBAL} = File::Spec->devnull();
            $ENV{GIT_TERMINAL_PROMPT} = 0;
            eval {
                my @argv = map { main::fs_native($_) } @command;
                $pid = IPC::Open3::open3( $input,
                    '>&' . fileno($stdout), '>&' . fileno($stderr), @argv );
                1;
            } or $spawn_error = $@ || 'child spawn failed';
        }
        chdir $previous or die "restore fixture directory: $!\n";
        die $spawn_error if defined $spawn_error;
        close $input or die "close child input: $!\n";
        my $waited;
        while (1) {
            $waited = waitpid( $pid, 0 );
            next if $waited == -1 && $! == Errno::EINTR();
            last;
        }
        my $status = $?;
        die "waitpid failed for fixture child\n" if $waited != $pid;
        # Perl exposes a Unix-style wait word even on Win32, where the
        # POSIX::WIF* helpers are unavailable. Signal interpretation is
        # valid only on hosts with POSIX child termination semantics.
        my $signal = $status & 127;
        my $code = $status < 0 ? 255
          : !$signal ? $status >> 8
          : $^O ne 'MSWin32' && $signal != 127 ? 128 + $signal
          : 255;
        seek $stdout, 0, 0 or die "rewind child stdout: $!\n";
        seek $stderr, 0, 0 or die "rewind child stderr: $!\n";
        local $/ = undef;
        my $out = <$stdout>;
        my $err = <$stderr>;
        close $stdout or die "close child stdout: $!\n";
        close $stderr or die "close child stderr: $!\n";
        return {
            code   => $code,
            stdout => defined $out ? $out : '',
            stderr => defined $err ? $err : '',
        };
    }

    sub git {
        my ( $self, @arguments ) = @_;
        my $result = $self->run_command( $self->{root}, 'git',
            '-C', $self->{root}, @arguments );
        die "fixture Git failed: $result->{stderr}\n" if $result->{code};
        return $result->{stdout};
    }

    sub commit {
        my ( $self, $message ) = @_;
        $self->git( 'add', '-A' );
        $self->git( 'commit', '-q', '-m', $message );
        return;
    }

    sub run_validator {
        my ( $self, @arguments ) = @_;
        return $self->run_from( $self->{root}, @arguments );
    }

    sub run_from {
        my ( $self, $cwd, @arguments ) = @_;
        return $self->run_command( $cwd, $^X,
            $self->path('scripts/validate-agent-knowledge.pl'), @arguments );
    }

    sub update_agent {
        my ($self) = @_;
        $self->write( 'AGENTS.md', $self->read('AGENTS.md')
              . "\nWhen parser boundaries change, update examples and rerun compatibility review.\n" );
        return;
    }
}

sub contract_case {
    my ( $name, $body, $root_name ) = @_;
    subtest $name => sub {
        my $fixture = ValidatorFixture->new($root_name);
        $body->($fixture);
        done_testing();
    };
    return;
}

sub exit_is {
    my ( $result, $code, $message ) = @_;
    is( $result->{code}, $code, $message )
      or diag( $result->{stdout} . $result->{stderr} );
    return;
}

contract_case 'clean worktree passes' => sub {
    my ($f) = @_;
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 0, 'clean worktree exit status' );
    like( $result->{stdout}, qr/Status: PASS/, 'pass summary' );
    like( $result->{stdout}, qr/Errors: 0/, 'zero errors' );
};

contract_case 'code change without knowledge update fails' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'unsynchronized code fails' );
    like( $result->{stdout}, qr/\[FAIL\] AK001/, 'AK001 names the failure' );
};

contract_case 'code and agent update passes with review warning' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->update_agent();
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 0, 'substantive update passes' );
    like( $result->{stdout}, qr/\[PASS\] AK001/, 'synchronization passes' );
    like( $result->{stdout}, qr/\[WARN\] AK004/, 'semantic relevance still requires review' );
};

contract_case 'code and skill update passes' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    my $path = '.agents/skills/example/SKILL.md';
    $f->write( $path, $f->read($path)
          . "\nWhen this parser changes, retain compatibility examples in this skill.\n" );
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'owning skill update passes' );
};

contract_case 'cosmetic knowledge change fails' => sub {
    my ($f) = @_;
    my $text = $f->read('AGENTS.md');
    $text =~ s/operational guidance/operational  guidance/;
    $f->write( 'AGENTS.md', $text );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'whitespace is not a substantive update' );
    like( $result->{stdout}, qr/\[FAIL\] AK004/, 'cosmetic update flagged' );
};

contract_case 'malformed skill fails structure' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->update_agent();
    $f->write( '.agents/skills/example/SKILL.md', "---\nname: example\n---\n# Broken\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'broken skill rejected' );
    like( $result->{stdout}, qr/\[FAIL\] AK002/, 'AK002 structure failure' );
};

contract_case 'broken local reference fails' => sub {
    my ($f) = @_;
    $f->update_agent();
    $f->write( 'AGENTS.md', $f->read('AGENTS.md') . "\nSee [missing](docs/missing.md).\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'broken link rejected' );
    like( $result->{stdout}, qr/\[FAIL\] AK003/, 'AK003 reference failure' );
};

contract_case 'untracked project file is discovered' => sub {
    my ($f) = @_;
    $f->write( 'src/new.cpp', "int value = 3;\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'untracked code needs synchronization' );
    like( $result->{stdout}, qr/src\/new\.cpp/, 'untracked path included' );
};

contract_case 'renamed project file is discovered' => sub {
    my ($f) = @_;
    $f->git( 'mv', 'src/example.cpp', 'src/renamed.cpp' );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'rename needs synchronization' );
    like( $result->{stdout}, qr/\[FAIL\] AK001/, 'rename is a project change' );
};

contract_case 'deleted project file is discovered' => sub {
    my ($f) = @_;
    unlink $f->path('src/example.cpp') or die "remove fixture: $!\n";
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'deletion needs synchronization' );
    like( $result->{stdout}, qr/src\/example\.cpp/, 'deleted path included' );
};

contract_case 'staged and unstaged changes combine' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->git( 'add', 'src/example.cpp' );
    $f->update_agent();
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'both change domains participate' );
};

contract_case 'valid range uses base and head' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->update_agent();
    $f->commit('code and knowledge');
    my $head = $f->git( 'rev-parse', 'HEAD' );
    $head =~ s/\s+\z//;
    exit_is( $f->run_validator( '--scope', 'range', '--base', $f->{base}, '--head', $head ),
        0, 'explicit committed range passes' );
};

contract_case 'missing revision is an execution error' => sub {
    my ($f) = @_;
    my $result = $f->run_validator( '--scope', 'range', '--base', 'missing', '--head', 'HEAD' );
    exit_is( $result, 2, 'invalid revision exits two' );
    like( $result->{stdout}, qr/\[FAIL\] AK005/, 'revision failure classified' );
};

contract_case 'incompatible configuration is an execution error' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml', "[validator]\ncontract_version = \"9.9.9\"\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 2, 'contract version mismatch exits two' );
    like( $result->{stdout}, qr/\[FAIL\] AK005/, 'configuration failure classified' );
};

contract_case 'incompatible CLI arguments are usage errors' => sub {
    my ($f) = @_;
    my $result = $f->run_validator( '--scope', 'range', '--base', $f->{base} );
    exit_is( $result, 2, 'missing head is a usage error' );
    like( $result->{stderr}, qr/requires both --base and --head/, 'usage explains required pair' );
};

contract_case 'help documents standard arguments' => sub {
    my ($f) = @_;
    my $result = $f->run_validator('--help');
    exit_is( $result, 0, 'help exits successfully' );
    like( $result->{stdout}, qr/--scope \{worktree,range\}/, 'scope choices documented' );
    like( $result->{stdout}, qr/--format \{text,json\}/, 'output choices documented' );
};

contract_case 'outside Git is an execution error' => sub {
    my ($f) = @_;
    my $outside = File::Spec->catdir( "$f->{temporary}", 'outside' );
    make_path($outside);
    my $result = $f->run_from( $outside, '--scope', 'worktree' );
    exit_is( $result, 2, 'outside repository exits two' );
    like( $result->{stdout}, qr/\[FAIL\] AK005/, 'repository discovery failure classified' );
};

contract_case 'JSON report retains schema and rule identities' => sub {
    my ($f) = @_;
    my $result = $f->run_validator( '--scope', 'worktree', '--format', 'json' );
    exit_is( $result, 0, 'JSON reporting succeeds' );
    my $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    is( $report->{schema_version}, '1.0', 'stable schema version' );
    is( $report->{contract_version}, '1.0.0', 'stable contract version' );
    is( $report->{status}, 'PASS', 'machine-readable result' );
    is( scalar @{ $report->{checks} }, 7, 'all standard and configured checks' );
    is_deeply( [ map { $_->{rule_id} } @{ $report->{checks} } ],
        [qw(AK001 AK002 AKX001 AK003 AK004 AK005 AK006)], 'stable report order' );
    is( $report->{summary}{errors}, scalar @{ $report->{errors} }, 'error summary matches details' );
    is( $report->{summary}{warnings}, scalar @{ $report->{warnings} }, 'warning summary matches details' );
};

contract_case 'explicit exclusion does not trigger synchronization' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml',
        "[validator]\ncontract_version = \"1.0.0\"\nexclude_paths = [\"generated/**\"]\n" );
    $f->commit('configure generated path exclusion');
    $f->write( 'generated/output.txt', "Generated output.\n" );
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'explicit generated exclusion honored' );
};

contract_case 'conflicting directives remain review warnings' => sub {
    my ($f) = @_;
    $f->write( 'AGENTS.md', $f->read('AGENTS.md')
          . "\nOperators must retain exclusive control of validation results.\n"
          . "Operators must not retain exclusive control of validation results.\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 0, 'conflict warning does not invent a policy failure' );
    like( $result->{stdout}, qr/\[WARN\] AK006/, 'literal contradiction needs review' );
};

# The original black-box cases above retain their behavioral
# contract. The following cases protect the port's new parser/process
# boundaries and documented configuration extensions.
contract_case 'range comparison starts at the merge base' => sub {
    my ($f) = @_;
    $f->git( 'checkout', '-q', '-b', 'side' );
    $f->write( 'src/side.cpp', "int side = 1;\n" );
    $f->commit('side-only change');
    my $side = $f->git( 'rev-parse', 'HEAD' );
    $side =~ s/\s+\z//;
    $f->git( 'checkout', '-q', 'main' );
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->update_agent();
    $f->commit('main code and knowledge');
    exit_is( $f->run_validator( '--scope', 'range', '--base', $side, '--head', 'HEAD' ),
        0, 'divergent base uses common ancestor' );
};

contract_case 'revision operands cannot invoke a shell or Git options' => sub {
    my ($f) = @_;
    for my $revision ( 'HEAD; touch injection-marker', '--output=injection-marker', '`touch injection-marker`' ) {
        my $result = $f->run_validator( '--scope', 'range', "--base=$revision", '--head=HEAD' );
        exit_is( $result, 2, 'invalid revision is refused as data' );
        ok( !-e $f->path('injection-marker'), 'revision never executes a command or output option' );
        like( $result->{stdout}, qr/\[FAIL\] AK005/, 'invalid operand classified as execution error' );
    }
};

contract_case 'invalid CLI combinations cannot fall through to validation' => sub {
    my ($f) = @_;
    for my $arguments (
        [],
        [ '--scope', 'invalid' ],
        [ '--scope', 'worktree', '--base', 'HEAD' ],
        [ '--scope', 'worktree', '--format', 'yaml' ],
        [ '--scope', 'range', '--head', 'HEAD' ],
        [ '--scope', 'worktree', '--unexpected' ],
        [ '--h' ],
        [ '--scope', 'worktree', 'extra-operand' ],
      )
    {
        my $result = $f->run_validator( @{$arguments} );
        exit_is( $result, 2, 'invalid CLI is a usage error' );
        isnt( $result->{stderr}, '', 'usage error explains the failure on stderr' );
    }
};

contract_case 'unique long-option abbreviations remain compatible' => sub {
    my ($f) = @_;
    my $result = $f->run_validator( '--sc', 'worktree', '--fo', 'json' );
    exit_is( $result, 0, 'unambiguous option abbreviations accepted' );
    my $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    is( $report->{status}, 'PASS', 'abbreviated format still selects JSON' );
    $result = $f->run_validator('--hel');
    exit_is( $result, 0, 'unambiguous help abbreviation accepted' );
    like( $result->{stdout}, qr/--scope/, 'help still documents complete option names' );
};

contract_case 'JSON errors preserve failure and execution exit codes' => sub {
    my ($f) = @_;
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    my $result = $f->run_validator( '--scope', 'worktree', '--format', 'json' );
    exit_is( $result, 1, 'policy failure retains exit one in JSON mode' );
    my $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    is( $report->{status}, 'FAIL', 'JSON records policy failure' );
    ok( scalar( grep { $_->{rule_id} eq 'AK001' } @{ $report->{errors} } ),
        'error details retain AK001' );
    $result = $f->run_validator( '--scope', 'range', '--base', 'missing', '--head', 'HEAD', '--format', 'json' );
    exit_is( $result, 2, 'execution failure retains exit two in JSON mode' );
    $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    ok( scalar( grep { $_->{rule_id} eq 'AK005' } @{ $report->{errors} } ),
        'execution error details retain AK005' );
    isnt( $result->{stderr}, '', 'execution detail is separate from JSON stdout' );
};

sub shell_quote {
    my ($value) = @_;
    $value =~ s/'/'\\''/g;
    return "'$value'";
}

sub make_execution_marker {
    my ( $f, $name ) = @_;
    my $script = File::Spec->catfile( "$f->{temporary}", "$name.pl" );
    my $marker = "$script.marker";
    my $fixture = File::Spec->catfile( $source_root, 'tests',
        'agent_knowledge_validator', 'fixtures', 'execution-marker.pl' );
    copy( fs_native($fixture), fs_native($script) )
      or die "copy execution fixture: $!\n";
    chmod 0700, $script or die "set execution fixture mode: $!\n";
    return ( $script, $marker );
}

contract_case 'Git diff drivers cannot execute during inspection' => sub {
    my ($f) = @_;
    $f->write( '.gitattributes', "src/*.cpp diff=contract-sentinel\n" );
    $f->commit('configure harmless diff attribute');
    my ( $converter, $converted ) = make_execution_marker( $f, 'textconv' );
    my ( $external, $executed ) = make_execution_marker( $f, 'external-diff' );
    $f->git( 'config', 'diff.contract-sentinel.textconv', shell_quote($^X) . ' ' . shell_quote($converter) );
    $f->git( 'config', 'diff.external', shell_quote($^X) . ' ' . shell_quote($external) );
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->update_agent();
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'diff inspection still reports ordinary changes' );
    ok( !-e $converted, 'textconv driver never executes' );
    ok( !-e $executed, 'external diff driver never executes' );
    exit_is( $f->run_command( $f->{root}, $^X, $converter ), 0, 'textconv marker fixture is executable' );
    ok( -e $converted, 'positive control proves the marker can be written' );
};

contract_case 'Git fsmonitor hooks cannot execute during discovery' => sub {
    my ($f) = @_;
    SKIP: {
        skip 'POSIX executable-hook fixture is not portable to Win32', 4 if $^O eq 'MSWin32';
        my ( $hook, $marker ) = make_execution_marker( $f, 'fsmonitor' );
        $f->git( 'config', 'core.fsmonitor', $hook );
        exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'status inspection keeps a clean fixture clean' );
        ok( !-e $marker, 'fsmonitor hook never executes' );
        exit_is( $f->run_command( $f->{root}, $^X, $hook ), 0, 'fsmonitor marker fixture is executable' );
        ok( -e $marker, 'positive control proves the marker can be written' );
    }
};

contract_case 'quoted dotted inline and Unicode TOML settings' => sub {
    my ($f) = @_;
    $f->write( 'policies/team #1.md', "# Policy\n\n## Review process\n\nRetain é and 😀 in reference fixtures.\n" );
    $f->write( '.agent-knowledge-validator.toml', <<'TOML' );
[validator]
contract_version = '1.0.0'
exclude_paths = [
  'generated/**', # trailing commas/comments are legal
]
instruction_globs = ['policies/*.md']
minimum_changed_lines = +1
inline_path_checks = false
required_headings = { 'policies/team #1.md' = ['Review process'] }
required_phrases = { "policies/team #1.md" = ["Retain \u00E9 and \U0001F600 in reference fixtures."] }
strict_text = { 'policies/*.md' = ['final_newline', 'no_nul'] }
skill_reference_prefixes = []
TOML
    $f->commit('configure quoted policy fixture');
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'all supported TOML forms preserve values' );
};

contract_case 'multiline literal and basic TOML strings' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml', <<'TOML' );
validator.contract_version = """1.0.0"""
validator.minimum_changed_lines = 1_0
validator.inline_path_checks = false
validator.required_phrases."AGENTS.md" = [
  """Keep operational \
      guidance accurate and reusable.""",
  '''Keep operational guidance accurate and reusable.''',
]
TOML
    $f->commit('configure multiline policy fixture');
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'multiline folding and literal strings decoded' );
};

contract_case 'TOML permits an implicit table parent to be declared later' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml', <<'TOML' );
[validator.required_phrases]
"AGENTS.md" = ['Keep operational guidance accurate and reusable.']
[validator]
contract_version = '1.0.0'
inline_path_checks = false
TOML
    $f->commit('configure implicit parent table');
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'implicit parent can receive its own table header' );
};

contract_case 'invalid TOML and configuration types fail closed' => sub {
    my ($f) = @_;
    my @invalid = (
        [ 'unknown table', "[unexpected]\nkey = true\n" ],
        [ 'unknown setting', "[validator]\nrun_command = \"ignored\"\n" ],
        [ 'string instead of array', "[validator]\nexclude_paths = \"generated/**\"\n" ],
        [ 'mixed array type', "[validator]\nexclude_paths = [true]\n" ],
        [ 'parent traversal', "[validator]\nexclude_paths = [\"../outside\"]\n" ],
        [ 'absolute path', "[validator]\ninstruction_globs = [\"/outside/*\"]\n" ],
        [ 'nonboolean flag', "[validator]\ninline_path_checks = \"false\"\n" ],
        [ 'boolean is not an integer', "[validator]\nminimum_changed_lines = true\n" ],
        [ 'zero minimum', "[validator]\nminimum_changed_lines = 0\n" ],
        [ 'oversized minimum', "[validator]\nminimum_changed_lines = 1001\n" ],
        [ 'unknown text rule', "[validator.strict_text]\n\"AGENTS.md\" = [\"silent\"]\n" ],
        [ 'unknown instruction path', "[validator.required_headings]\n\"missing.md\" = [\"Review\"]\n" ],
        [ 'duplicate key', "[validator]\ncontract_version = '1.0.0'\ncontract_version = '1.0.0'\n" ],
        [ 'duplicate table', "[validator]\n[validator]\n" ],
        [ 'dotted table cannot reopen',
            "[validator]\nrequired_phrases.\"AGENTS.md\" = ['Keep operational guidance accurate and reusable.']\n"
              . "[validator.required_phrases]\n" ],
        [ 'inline table cannot reopen',
            "[validator]\nrequired_phrases = {}\n[validator.required_phrases]\n" ],
        [ 'excessive nesting', "[validator]\nexclude_paths = " . ( '[' x 129 ) . '1' . ( ']' x 129 ) . "\n" ],
        [ 'signed integer overflow', "[validator]\nminimum_changed_lines = 9223372036854775808\n" ],
        [ 'unterminated array', "[validator]\nexclude_paths = [\"generated/**\"\n" ],
        [ 'invalid string escape', '[validator]' . "\n" . 'contract_version = "1.0.\q"' . "\n" ],
        [ 'surrogate escape', '[validator]' . "\n" . 'contract_version = "\uD800"' . "\n" ],
    );
    for my $case (@invalid) {
        $f->write( '.agent-knowledge-validator.toml', $case->[1] );
        my $result = $f->run_validator( '--scope', 'worktree' );
        exit_is( $result, 2, "$case->[0] rejected" );
        like( $result->{stdout}, qr/\[FAIL\] AK005/, "$case->[0] uses AK005" );
    }
};

contract_case 'configured minimum counts effective knowledge lines' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml', "[validator]\nminimum_changed_lines = 2\n" );
    $f->commit('configure effective-line minimum');
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->update_agent();
    exit_is( $f->run_validator( '--scope', 'worktree' ), 1, 'one effective line does not meet two-line requirement' );
    $f->write( 'AGENTS.md', $f->read('AGENTS.md')
          . "Retain focused negative cases whenever parser trust boundaries change.\n" );
    exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'two substantive lines meet configured minimum' );
};

contract_case 'configured structural and text checks remain enforced' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml', <<'TOML' );
[validator.required_headings]
"AGENTS.md" = ['Continuous Agent & Skills Maintenance']
[validator.strict_text]
"AGENTS.md" = ['final_newline', 'no_nul', 'no_trailing_whitespace']
TOML
    $f->commit('configure text constraints');
    $f->update_agent();
    $f->write( 'AGENTS.md', $f->read('AGENTS.md') . "Keep parser fixtures scoped and reproducible. \n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'configured whitespace defect rejected' );
    like( $result->{stdout}, qr/\[FAIL\] AKX001/, 'extension retains its rule identity' );
};

contract_case 'configured LF check sees carriage returns in raw UTF-8' => sub {
    my ($f) = @_;
    $f->write( '.agent-knowledge-validator.toml',
        "[validator.strict_text]\n\"AGENTS.md\" = ['lf_only']\n" );
    $f->commit('configure LF-only instructions');
    $f->update_agent();
    my $text = $f->read('AGENTS.md');
    $text =~ s/\r?\n/\r\n/g;
    $f->write( 'AGENTS.md', $text );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'CRLF is not normalized before the LF-only rule' );
    like( $result->{stdout}, qr/\[FAIL\] AKX001/, 'line-ending defect uses the configured rule identity' );
};

contract_case 'local references reject repository escape' => sub {
    my ($f) = @_;
    $f->update_agent();
    $f->write( 'AGENTS.md', $f->read('AGENTS.md') . "\nSee [outside](../external.md).\n" );
    my $result = $f->run_validator( '--scope', 'worktree' );
    exit_is( $result, 1, 'traversing reference rejected' );
    like( $result->{stdout}, qr/\[FAIL\] AK003/, 'reference escape uses AK003' );
};

contract_case 'UTF-8 repository paths and encoded Markdown links agree' => sub {
    my ($f) = @_;
    $f->write( 'docs/資料/résumé.md', "Unicode reference fixture.\n" );
    $f->update_agent();
    $f->write( 'AGENTS.md', $f->read('AGENTS.md')
          . "\nSee [literal](docs/資料/résumé.md).\n"
          . "See [encoded](docs/%E8%B3%87%E6%96%99/r%C3%A9sum%C3%A9.md).\n"
          . "See [mixed](docs/資料/r%C3%A9sumé.md).\n" );
    my $result = $f->run_validator( '--scope', 'worktree', '--format', 'json' );
    exit_is( $result, 0, 'Unicode worktree root and all link spellings resolve' );
    if ($result->{code} && $^O eq 'MSWin32') {
        # Only private fixture paths are recorded. Distinguish Git UTF-8
        # bytes from Perl's native path without altering either boundary.
        my $git_root = $f->git('rev-parse', '--show-toplevel');
        $git_root =~ s/\r?\n\z//;
        diag('Win32 Unicode fixture root UTF-8 bytes: '
            . unpack('H*', substr(Encode::encode('UTF-8', $f->{root}), 0, 1024)));
        diag('Git worktree root bytes: '
            . unpack('H*', substr($git_root, 0, 1024)));
        my $native = fs_native(fs_text($git_root));
        diag('Native root Unicode flag: ' . (utf8::is_utf8($native) ? 1 : 0));
        my $resolved = abs_path($native);
        diag('Native root resolution: ' . (defined($resolved) ? 'present' : "failed: $!"));
    }
    my $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    is( $report->{status}, 'PASS', 'UTF-8 JSON output remains machine-readable' );
    is( scalar @{ $report->{errors} }, 0, 'literal and decoded paths have no reference failures' );
}, 'repository résumé';

contract_case 'Git NUL-delimited changes preserve newline filenames' => sub {
    my ($f) = @_;
    SKIP: {
    skip 'Win32 filenames cannot contain newline characters', 2 if $^O eq 'MSWin32';
    $f->write( '.agent-knowledge-validator.toml',
        "[validator]\ninstruction_globs = ['policies/*.md']\n" );
    my $name = "policies/line\nbreak.md";
    $f->write( $name, "Initial operational instructions.\n" );
    $f->commit('configure newline filename instruction');
    $f->write( 'src/example.cpp', "int value = 2;\n" );
    $f->write( $name,
        "Initial operational instructions.\nRetain exact paths when reviewing parser trust boundaries.\n" );
    my $result = $f->run_validator( '--scope', 'worktree', '--format', 'json' );
    exit_is( $result, 0, 'newline-containing instruction synchronizes code' );
    my $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    my ($synchronization) = grep { $_->{rule_id} eq 'AK001' } @{ $report->{checks} };
    ok( scalar( grep { $_ eq $name } @{ $synchronization->{files} } ),
        'JSON retains the exact newline-containing filename' );
    }
};

contract_case 'Git pathspec metacharacters are literal knowledge paths' => sub {
    my ($f) = @_;
    SKIP: {
    skip 'Win32 filenames cannot contain an asterisk', 3 if $^O eq 'MSWin32';
    $f->write( '.agent-knowledge-validator.toml',
        "[validator]\ninstruction_globs = ['policies/*.md']\n" );
    my $literal = 'policies/[ab]*.md';
    my $decoy = 'policies/a-decoy.md';
    $f->write( $literal, "Retain operational guidance exactly for this literal filename.\n" );
    $f->write( $decoy, "Initial guidance for an independent parser.\n" );
    $f->commit('configure literal pathspec instruction');
    $f->write( $literal, "Retain  operational guidance exactly for this literal filename.\n" );
    $f->write( $decoy,
        "Initial guidance for an independent parser.\nReview bounds and lifetime authority when modifying input parsers.\n" );
    my $result = $f->run_validator( '--scope', 'worktree', '--format', 'json' );
    exit_is( $result, 1, 'decoy substantive diff cannot disguise a cosmetic literal-path edit' );
    my $report = JSON::PP->new->utf8->decode( $result->{stdout} );
    my ($relevance) = grep { $_->{rule_id} eq 'AK004' } @{ $report->{checks} };
    is( $relevance->{status}, 'FAIL', 'literal cosmetic edit remains suspicious' );
    ok( scalar( grep { $_ eq $literal } @{ $relevance->{files} } ),
        'literal metacharacter path appears in the failure report' );
    }
};

contract_case 'symlink references cannot escape the repository' => sub {
    my ($f) = @_;
    my $outside = File::Spec->catfile( "$f->{temporary}", 'external.md' );
    open my $external, '>', $outside or die "create external fixture: $!\n";
    print {$external} "External fixture.\n";
    close $external or die "close external fixture: $!\n";
    my ($linked, $reason) =
      create_fixture_symlink($outside, $f->path('external-link.md'));
    SKIP: {
        skip "host cannot create symbolic links: $reason", 2 unless $linked;
        $f->update_agent();
        $f->write( 'AGENTS.md', $f->read('AGENTS.md') . "\nSee [outside](external-link.md).\n" );
        my $result = $f->run_validator( '--scope', 'worktree' );
        exit_is( $result, 1, 'symlink traversal rejected' );
        like( $result->{stdout}, qr/\[FAIL\] AK003/, 'symlink escape uses AK003' );
    }
};

contract_case 'instruction discovery does not follow symlink files' => sub {
    my ($f) = @_;
    my $outside = File::Spec->catfile( "$f->{temporary}", 'external-instruction.md' );
    open my $external, '>', $outside or die "create external fixture: $!\n";
    print {$external} "Malformed external instructions.\n";
    close $external or die "close external fixture: $!\n";
    make_path( $f->path('external') );
    my ($linked, $reason) =
      create_fixture_symlink($outside, $f->path('external/AGENTS.md'));
    SKIP: {
        skip "host cannot create symbolic links: $reason", 1 unless $linked;
        $f->commit('record external instruction symlink');
        exit_is( $f->run_validator( '--scope', 'worktree' ), 0, 'external instruction contents are not consumed' );
    }
};

done_testing();

=head1 NAME

test_validator.t - black-box Agent Knowledge Validation contract tests

=head1 DESCRIPTION

Runs the Perl CLI in isolated Git repositories and checks the original
contract cases plus TOML, path-containment and process-argument
regressions. No project binary, compiler, network service or external Perl
module is required. Git commits created by these tests belong only to
File::Temp fixtures. Source library symlinks are refused when assembling
the fixture. Symbolic-link checks skip only where Win32 cannot create them;
POSIX hook and Win32-forbidden filename fixtures have explicit platform skips.

=head1 RUNNING

Run C<prove -v tests/agent_knowledge_validator/test_validator.t> in manual
CI. Exit status and TAP output use Test::More conventions. Child stdout
and stderr are captured separately in temporary regular files, avoiding
pipe-readiness differences and pipe-capacity deadlocks across platforms.
The owning manual CI/CTest lane supplies the execution timeout.

=cut
