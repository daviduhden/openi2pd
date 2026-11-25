#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: perl portability.
#
# Static guard against Python sources/shebangs, non-core Perl modules,
# dangerous shell invocations and build files that still call Python. It scans
# the maintained scripts and CMake wiring without building or running project
# programs.
#

use strict;
use warnings;
use 5.016;

use File::Basename qw(basename);
use File::Find     qw(find);
use File::Spec;
use FindBin qw($Bin);
use Test::More;

my $tools =
  File::Spec->rel2abs( File::Spec->catdir( $Bin, File::Spec->updir ) );
my $root =
  File::Spec->rel2abs( File::Spec->catdir( $tools, File::Spec->updir ) );

sub read_file {
    my ($path) = @_;
    open my $fh, '<', $path or return;
    local $/ = undef;
    my $text = <$fh>;
    close $fh;
    return defined $text ? $text : '';
}

# Repository-relative paths are comparisons and diagnostics, not
# filesystem access. File::Spec->abs2rel() returns the native separator
# (a backslash on Windows) while the inventory below is written with '/',
# so every relative value is rebuilt from File::Spec->splitdir(): the
# result is separator independent and matches the manifest on every
# platform.
sub repo_relative {
    my ($path) = @_;
    return join '/',
      File::Spec->splitdir( File::Spec->abs2rel( $path, $root ) );
}

sub first_line {
    my ($path) = @_;
    open my $fh, '<', $path or return;

    # A shebang is far shorter than this; do not pull a large binary asset
    # into memory merely because the inventory scan visits it.
    my $line = '';
    read( $fh, $line, 512 );
    close $fh;
    $line =~ s/ ^\x{FEFF} //x;
    return $line;
}

my %skip_directory = map { $_ => 1 } qw(
  .git .cache .gradle .venv __pycache__ build-output dist node_modules
  out target venv
);

# Generated and dependency trees are not project sources; scanning them would
# report third-party files that the project does not maintain.
my ( @python_files, @python_shebang, @perl_modules );
my $python_project_file =
  qr/ SConstruct|SConscript|wscript|pyproject\.toml|setup\.cfg /ix;
my $python_requirements_file = qr/ requirements[^\/]*\.txt /ix;

find(
    {
        no_chdir   => 1,
        preprocess => sub {
            return grep { !$skip_directory{$_} } @_;
        },
        wanted => sub {
            my $path = $File::Find::name;
            return if -d $path;
            my $name     = basename($path);
            my $relative = repo_relative($path);
            push @perl_modules, $relative
              if $relative =~ m{ \Ascripts/lib/.*\.pm\z }x;
            push @python_files, $relative
              if ( $name =~ / \.(?:py|pyw|pyi|pyx|pxd|ipynb)\z /ix
                || $name =~ / \.py\.in\z /ix
                || $name =~
                / \A(?:$python_project_file|$python_requirements_file)\z /x );

            my $prefix = first_line($path);
            if ( defined $prefix ) {

                # A shebang must be on the first line, but a BOM or a leading
                # blank line is tolerated; only the first three lines are
                # considered so a comment further down cannot match.
                my @head    = split / \n /x, $prefix;
                my $shebang = join "\n",
                  @head[ 0 .. ( $#head < 2 ? $#head : 2 ) ];
                if (
                    (
                           $shebang =~ / ^\#!.*\b(?:python|pypy)[0-9.]*\b /imx
                        || $shebang =~ / ^\#!.*\b(?:python|pypy)[0-9.]*\b /ix
                    )
                  )
                {
                    push @python_shebang, $relative;
                }
            }
        },
    },
    $root
);

is_deeply( \@python_files,   [], 'no project-maintained Python sources' );
is_deeply( \@python_shebang, [], 'no Python shebangs in maintained files' );

my @scripts = (
    'build/windows-manual.pl',
    'scripts/validate-agent-knowledge.pl',
    'tests/agent_knowledge_validator/test_validator.t',
    'tests/agent_knowledge_validator/fixtures/execution-marker.pl',
    'tests/android-javadoc-regex.t',
    'tools/scripts/i2pdctl',
    'tools/scripts/check-doc-links.pl',
    'tools/scripts/check-doc-nav.pl',
    'tools/scripts/check-doxygen-params.pl',
    'tools/scripts/validate-branding.pl',
    'tools/scripts/check-workflows.pl',
    'tools/scripts/check-shell-policy.pl',
    'tools/tests/cpp-comment-integrity.t',
    'tools/tests/i2pdctl.t',
    'tools/tests/validate-branding.t',
    'tools/tests/perl-portability.t',
    'tests/interop/check-vectors.pl',
    'android/tools/compact-vector-paths.pl',
    'android/tools/check-android-locales.pl',
    'android/tools/check-android-javadoc.pl',
    'android/tools/check-android-jni-symbols.pl',
    'i18n/check-i18n-catalogs.pl',
    'tools/tests/maintenance-checkers.t',
    'tools/tests/make-drivers.t',
    'tools/tests/interop-helpers.t',
    'tools/tests/fixtures/record-build-command.pl',
    'tools/tests/fixtures/vector-status-stub.pl',
    'tools/tests/fixtures/signal-child.pl',
    'tests/interop/hex-to-binary.pl',
    'tests/interop/sam-stream-send.pl',
    sort @perl_modules,
);

my @discovered = map { repo_relative($_) }
  grep { -f $_ } (
    glob( File::Spec->catfile( $root, 'build', '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'tools', 'scripts', '*' ) ),
    glob( File::Spec->catfile( $root, 'tools', 'tests',   '*.t' ) ),
    glob( File::Spec->catfile( $root, 'tools', 'tests', 'fixtures', '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'tests',   'interop', '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'android', 'tools',   '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'i18n',    '*.pl' ) )
  );

# Standalone contracts live directly under tests as well as subsystem
# directories. Discover this class independently of its CMake inventory.
push @discovered, map { repo_relative($_) }
  grep { -f $_ } glob( File::Spec->catfile( $root, 'tests', '*.t' ) );
push @discovered,
  map  { repo_relative($_) }
  grep { -f $_ } (
    glob( File::Spec->catfile( $root, 'scripts', '*.pl' ) ),
    glob(
        File::Spec->catfile(
            $root, 'tests', 'agent_knowledge_validator', '*.t'
        )
    ),
    glob(
        File::Spec->catfile(
            $root, 'tests', 'agent_knowledge_validator', 'fixtures', '*.pl'
        )
    )
  );
push @discovered, @perl_modules;
is_deeply(
    [ sort @discovered ],
    [ sort @scripts ],
    'maintained Perl inventory matches discovered scripts'
);

# Regression for the Windows inventory failure: abs2rel() returns the
# native separator, so an unnormalised value discovered on Windows
# ('android\tools\x.pl') never matched the '/'-separated manifest.
# Everything reported by this suite is rebuilt from splitdir().
my $native_example =
  File::Spec->catfile( $root, 'tools', 'scripts', 'i2pdctl' );
like(
    repo_relative($native_example),
    qr{ \Atools/scripts/i2pdctl\z }x,
    'repository-relative paths use / on every platform'
);
unlike( join( "\n", @discovered ),
    qr{ \\ }x, 'discovered inventory contains no native separators' );
SKIP: {
    skip 'native separator is / on this platform', 1
      unless $^O eq 'MSWin32' || $^O eq 'msys';
    my $native = $root . '\\tools\\scripts\\i2pdctl';
    like(
        repo_relative($native),
        qr{ \Atools/scripts/i2pdctl\z }x,
        'a backslash path is normalised before comparison'
    );
}

# Modules shipped with the Perl core on OpenBSD/Linux. This also rejects
# accidental CPAN or runtime downloads. Imports of repository modules
# are permitted only when their namespace resolves to scripts/lib.
my %allowed_module = map { $_ => 1 } qw(
  strict warnings lib constant utf8
  Carp Config Cwd Digest::SHA Encode Errno File::Basename File::Copy File::Find File::Path File::Spec File::Temp
  FindBin Getopt::Long HTTP::Tiny IO::Select IO::Socket::INET IPC::Open3
  JSON::PP POSIX Symbol Test::More Time::HiRes
);

sub module_namespace {
    my ($module) = @_;
    $module =~ s{ \Ascripts/lib/ }{}x;
    $module =~ s{ \.pm\z }{}x;
    $module =~ s{ / }{::}gx;
    return $module;
}

my %maintained_module = map { module_namespace($_) => 1 } @perl_modules;

sub source_modules {
    my ($source) = @_;

    # POD begins with a column-one directive and continues through =cut.
    $source =~
      s/ ^=[A-Za-z]\w*\b[^\n]*\n.*?(?:^=cut\b[^\n]*(?:\n|\z)|\z) //gmsx;
    return $source =~ / ^[ \t]*(?:use|require)\s+([A-Za-z_][A-Za-z0-9_:]*) /mgx;
}

is_deeply(
    [
        source_modules(
"use strict;\n\n=head1 DESCRIPTION\n\nrequire explicit support.\n\n=cut\n\nrequire Carp;\n"
        )
    ],
    [qw(strict Carp)],
    'module scan ignores POD and resumes after =cut'
);

my @dangerous = (
    [ qr/ (?<![\w:])system\s*\( /x,   'system invocation' ],
    [ qr/ (?<![\w:])exec\s*\( /x,     'exec invocation' ],
    [ qr/ (?<![\w:])qx\s*[\(\{\/] /x, 'qx invocation' ],
    [ qr/ (?<![\w:])eval\s+["'\$] /x, 'string eval' ],
    [
        qr/ (?<![\w:])(?:tempnam|tmpnam)\s*\( /x,
        'insecure temporary-name helper'
    ],
    [ qr/ (?<![\w:])chmod\s+0?777\b /x, 'world-writable chmod' ],
    [ qr/ (?<![\w:])chmod\s+0?666\b /x, 'world-writable chmod' ],
    [
        qr/ \bopen\b\s*\(?\s*[^,\n]+,\s*["'][^"']*[^"'\-]\|["'] /x,
        'single-scalar pipe open'
    ],
);

# Match the open keyword, not the prefix of a quoted OpenBSD shell name.
# Keep real scalar-pipe operands rejected beside that source fixture.
my $pipe_pattern = $dangerous[-1][0];
for my $command ( 'command |', 'command --argument |' ) {
    my $source = 'open' . " my \$fh, '$command';";
    like( $source, $pipe_pattern, 'scalar pipe operand remains rejected' );
}
my $file_read = 'open'
  . " my \$fh, '<', \$path;\n"
  . "for my \$required ('shell: openbsd {0}', 'run: |') {}\n";
unlike( $file_read, $pipe_pattern,
    'file read cannot consume a later quoted workflow literal' );

sub check_maintained_script {
    my ($relative) = @_;
    my $path = File::Spec->catfile( $root, split m{ / }x, $relative );
    ok( -f $path, "$relative exists" );
    my $text = read_file($path);
    return unless defined $text;

    my $line = first_line($path);
    like(
        $line,
        qr/ \A\#!\/usr\/bin\/env\ perl\b /x,
        "$relative uses a portable Perl shebang"
    ) if defined $line && $relative !~ / \.pm\z /x;
    like( $text, qr/ ^\s*use\s+strict\s*; /mx,   "$relative enables strict" );
    like( $text, qr/ ^\s*use\s+warnings\s*; /mx, "$relative enables warnings" );
    unlike(
        $text,
        qr/ ^\s*no\s+(?:strict|warnings)\b /mx,
        "$relative never disables strict or warnings"
    );
    unlike(
        $text,
        qr/ ^\#!.*\b(?:python|pypy)[0-9.]*\b /imx,
        "$relative has no Python shebang"
    );

    if (
        $^O ne 'MSWin32'
        && (   $relative =~ m{ \A(?:tools/scripts|android/tools|i18n)/ }x
            || $relative eq 'scripts/validate-agent-knowledge.pl'
            || $relative eq 'tests/interop/check-vectors.pl' )
      )
    {
        ok( -x $path, "$relative is executable" );
    }

    for my $module ( source_modules($text) ) {
        ok(
            $allowed_module{$module} || $maintained_module{$module},
            "$relative uses core or maintained module $module"
        );
    }

    for my $rule (@dangerous) {
        my ( $pattern, $description ) = @{$rule};
        unlike( $text, $pattern, "$relative avoids $description" );
    }
    return;
}

check_maintained_script($_) for @scripts;

my $cmake_i2pdctl     = qr{ "\$\{CMAKE_CURRENT_SOURCE_DIR\}/scripts/i2pdctl" }x;
my $cmake_module_list = qr/ \$\{I2PD_AGENT_KNOWLEDGE_MODULES\} /x;
my $tools_cmake =
  read_file( File::Spec->catfile( $root, 'tools', 'CMakeLists.txt' ) );
like(
    $tools_cmake,
    qr/ install\s*\(\s*PROGRAMS\s+$cmake_i2pdctl /sx,
    'CMake installs i2pdctl as a program'
);
like(
    $tools_cmake,
    qr/ find_package\s*\(\s*Perl\s*\) /x,
    'CMake discovers the Perl interpreter for static checks'
);
ok(
    -f File::Spec->catfile( $root, '.perlcriticrc' ),
    'optional Perl::Critic profile is present'
);

# Every maintained script except i2pdctl must be compiled by the syntax
# sweep; i2pdctl has its own perl-i2pdctl-syntax target. The optional
# critic target names every script, including i2pdctl and its own
# profile operand. Both lists are compared with the inventory above in
# both directions, so an omitted or stale entry fails here instead of
# silently skipping a syntax or style check.
sub cmake_list_entries {
    my ( $text, $pattern ) = @_;
    my ($block) = $text =~ $pattern;
    return () unless defined $block;
    my $operand =
      qr/ "\$\{(CMAKE_CURRENT_SOURCE_DIR|PROJECT_ROOT)\}\/([^"]+)" /x;
    my @entries;
    while ( $block =~ /$operand/gx ) {
        my ( $anchor, $tail ) = ( $1, $2 );
        my $relative =
          $anchor eq 'CMAKE_CURRENT_SOURCE_DIR' ? "tools/$tail" : $tail;

        # The critic command also names its profile; only script
        # operands belong to the inventory comparison.
        push @entries, $relative
          if $relative =~ / \.(?:pl|pm|t)\z /x || $relative =~ m{ /i2pdctl\z }x;
    }
    return @entries;
}

my @expected_syntax =
  grep { $_ ne 'tools/scripts/i2pdctl' && !/ \.pm\z /x } @scripts;
my @syntax_entries = cmake_list_entries( $tools_cmake,
    qr/ set\(I2PD_PERL_SYNTAX_SCRIPTS(.*?)\)\n /sx );
is_deeply(
    [ sort @syntax_entries ],
    [ sort @expected_syntax ],
    'the CMake syntax sweep registers every maintained script except i2pdctl'
);

my @critic_entries =
  cmake_list_entries( $tools_cmake,
    qr/ add_test\(NAME\ perl-critic(.*?)\)\n /sx );
is_deeply(
    [ sort @critic_entries ],
    [ sort grep { !/ \.pm\z /x } @scripts ],
    'the optional Perl::Critic target registers every maintained script'
);
my @cmake_modules = cmake_list_entries( $tools_cmake,
    qr/ set\(I2PD_AGENT_KNOWLEDGE_MODULES(.*?)\)\n /sx );
is_deeply(
    [ sort @cmake_modules ],
    [ sort @perl_modules ],
    'explicit CMake module inventory matches recursive source discovery'
);
like(
    $tools_cmake,
    qr/ list\(APPEND\ I2PD_PERL_SYNTAX_SCRIPTS\ $cmake_module_list\) /x,
    'validator modules participate in syntax coverage'
);
like(
    $tools_cmake,
    qr/ add_test\(NAME\ perl-critic.*?$cmake_module_list /sx,
    'validator modules participate in optional style coverage'
);

# Build consumers must not regress to Python. Documentation that records the
# historical migration is intentionally not included in this list.
my @consumers = (
    'tools/CMakeLists.txt',
    'tests/CMakeLists.txt',
    'build/CMakeLists.txt',
    'Makefile',
    'tests/Makefile',
    '.github/workflows/build-linux.yml',
    '.github/workflows/build-android.yml',
    '.github/workflows/validation.yml',
    '.github/workflows/build-linux-openssl3.yml',
    '.github/workflows/build-linux-openssl4.yml',
    '.github/workflows/crypto-tsan.yml',
    '.github/workflows/build-windows.yml',
    '.github/workflows/build-openbsd.yml',
    '.github/workflows/packaging.yml',
    '.github/workflows/agent-maintenance.yml',
    'debian/rules',
    'debian/control',
    'contrib/rpm/i2pd.spec',
    'contrib/rpm/i2pd-git.spec',
    'contrib/static/i2pd.spec',
    'contrib/static/package.sh',
    'contrib/docker/Dockerfile',
    'build/build_mingw.cmd',
    'build/win_installer.iss',
    'android/app/build.gradle',
    'android/build.gradle',
    'android/settings.gradle',
    'tests/check-build-system.cmake',
    'tests/check-i18n.cmake',
    'tests/check-tools-inventory.cmake',
    '.gitignore',
    '.dockerignore',
);

# An ignore file is data, not a command: its '#' lines are documentation
# may describe historical tools without invoking anything. Only the
# effective patterns are scanned; a real interpreter or package command
# in a pattern is still rejected.
my %comment_only_consumer = map { $_ => 1 } qw(.gitignore .dockerignore);
my $python_command     = qr/ \bpython[0-9.]*\b|\bpip[0-9.]*\b|\bpy\s+-3\b /ix;
my $python_environment = qr/ \bPYTHON(?:PATH|HOME)\b /ix;
my $python_environment_tool = qr/ \b(?:virtualenv|venv|tox|poetry)\b /ix;
for my $relative (@consumers) {
    my $path = File::Spec->catfile( $root, split m{ / }x, $relative );
    my $text = read_file($path);
    ok( -f $path, "$relative exists" );
    next unless defined $text;
    if ( $comment_only_consumer{$relative} ) {
        $text = join "\n", grep { !/ ^\s*\# /x } split / \n /x, $text;
    }
    unlike(
        $text,
        qr/ $python_command | $python_environment | $python_environment_tool /x,
        "$relative has no Python invocation"
    );
}

done_testing();

=head1 NAME

perl-portability.t - guard the maintained Perl sources and build wiring

=head1 SYNOPSIS

    prove -v tools/tests/perl-portability.t

=head1 DESCRIPTION

Test::More suite (C<done_testing>) that enforces the Perl policy without
compiling or running the project. It asserts that:

=over 4

=item *

no project-maintained Python source, SCons/setuptools metadata or Python
shebang exists outside the generated and dependency trees;

=item *

the maintained script inventory equals the scripts discovered under
F<tools/scripts/>, F<tools/tests/>, F<tests/interop/>, F<android/tools/> and
F<i18n/>, F<scripts/>, top-level F<tests/*.t> and
F<tests/agent_knowledge_validator/>. Validator
modules under F<scripts/lib/> are discovered recursively and checked for
core dependencies and safe source patterns without a program shebang.
Adding, renaming or removing a script therefore requires updating
the C<@scripts> list in this file. Both sides are repository-relative paths
with C</> separators on every platform: C<File::Spec-E<gt>abs2rel> returns the
native separator, so an unnormalised discovery compares as
C<android\tools\name.pl> on Windows and never matches the manifest;

=item *

each program has a portable Perl shebang; programs and modules enable
C<strict> and C<warnings> without disabling them again, carry no Python
shebang, and are executable on POSIX hosts where the layout requires it.
Imports must name either a core allowlisted module or a maintained
C<scripts/lib> namespace; this rejects a CPAN or runtime download;

=item *

each script avoids shell invocation, string C<eval>, insecure temporary-name
helpers, world-writable modes and single-scalar pipe opens;

=item *

F<tools/CMakeLists.txt> installs C<i2pdctl> as a program and discovers the
Perl interpreter, the optional F<.perlcriticrc> profile is present, the
compile-only syntax sweep and the optional Perl::Critic target each register
exactly the maintained inventory above (the syntax sweep covers everything
except C<i2pdctl>, which has its own target), and the build, packaging and
Android wiring files contain no Python invocation.

=back

=head1 OUTPUT AND EXIT STATUS

TAP output on STDOUT. The process exits non-zero when any assertion fails and
zero when the suite passes. Files are only read; no script, compiler, build or
network operation is performed by this suite.

=head1 DEPENDENCIES

Core Perl with Test::More, File::Find, File::Spec, File::Basename and FindBin.
The allowlist doubles as the dependency contract, so it must be extended
deliberately if a new core module is introduced.

=head1 PORTABILITY

The executable-bit assertion is skipped on Windows, where the mode is not
meaningful. Repository-relative values are rebuilt from
C<File::Spec-E<gt>splitdir> and joined with C</>, so the inventory comparison
is independent of the native path separator; a backslash path is asserted on
the platforms whose C<File::Spec> splits on both separators. Everything else is
platform-independent text inspection and runs with the OpenBSD base
interpreter.
