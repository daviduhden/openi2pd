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
    local $/;
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
    return join '/', File::Spec->splitdir( File::Spec->abs2rel( $path, $root ) );
}

sub first_line {
    my ($path) = @_;
    open my $fh, '<', $path or return;

    # A shebang is far shorter than this; do not pull a large binary asset
    # into memory merely because the inventory scan visits it.
    my $line = '';
    read( $fh, $line, 512 );
    close $fh;
    $line =~ s/^\x{FEFF}//;
    return $line;
}

my %skip_directory = map { $_ => 1 } qw(
  .git .cache .gradle .venv __pycache__ build-output dist node_modules
  out target venv
);

# Generated and dependency trees are not project sources; scanning them would
# report third-party files that the project does not maintain.
my ( @python_files, @python_shebang, @perl_modules );

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
              if $relative =~ m{\Ascripts/lib/.*\.pm\z};
            push @python_files, $relative
              if ( $name =~ /\.(?:py|pyw|pyi|pyx|pxd|ipynb)\z/i
                || $name =~ /\.py\.in\z/i
                || $name =~
/\A(?:SConstruct|SConscript|wscript|pyproject\.toml|setup\.cfg|requirements[^\/]*\.txt)\z/i
              );

            my $prefix = first_line($path);
            if ( defined $prefix ) {

                # A shebang must be on the first line, but a BOM or a leading
                # blank line is tolerated; only the first three lines are
                # considered so a comment further down cannot match.
                my @head    = split /\n/, $prefix;
                my $shebang = join "\n",
                  @head[ 0 .. ( $#head < 2 ? $#head : 2 ) ];
                if (
                    (   $shebang =~ /^#!.*\b(?:python|pypy)[0-9.]*\b/im
                        || $shebang =~ /^#!.*\b(?:python|pypy)[0-9.]*\b/i )
                  )
                {
                    push @python_shebang, $relative;
                }
            }
        },
    },
    $root
);

is_deeply( \@python_files, [], 'no project-maintained Python sources' );
is_deeply( \@python_shebang, [],
    'no Python shebangs in maintained files' );

my @scripts = (
    'scripts/validate-agent-knowledge.pl',
    'tests/agent_knowledge_validator/test_validator.t',
    'tests/agent_knowledge_validator/fixtures/execution-marker.pl',
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
    glob( File::Spec->catfile( $root, 'tools',   'scripts', '*' ) ),
    glob( File::Spec->catfile( $root, 'tools',   'tests',   '*.t' ) ),
    glob( File::Spec->catfile( $root, 'tools',   'tests', 'fixtures', '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'tests',   'interop', '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'android', 'tools',   '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'i18n',    '*.pl' ) )
  );
push @discovered, map { repo_relative($_) }
  grep { -f $_ } (
    glob( File::Spec->catfile( $root, 'scripts', '*.pl' ) ),
    glob( File::Spec->catfile( $root, 'tests', 'agent_knowledge_validator', '*.t' ) ),
    glob( File::Spec->catfile( $root, 'tests', 'agent_knowledge_validator', 'fixtures', '*.pl' ) )
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
    qr{\Atools/scripts/i2pdctl\z},
    'repository-relative paths use / on every platform'
);
unlike( join( "\n", @discovered ), qr{\\},
    'discovered inventory contains no native separators' );
SKIP: {
    skip 'native separator is / on this platform', 1
      unless $^O eq 'MSWin32' || $^O eq 'msys';
    my $native = $root . '\\tools\\scripts\\i2pdctl';
    like(
        repo_relative($native),
        qr{\Atools/scripts/i2pdctl\z},
        'a backslash path is normalised before comparison'
    );
}

# Modules shipped with the Perl core on OpenBSD/Linux. This also rejects
# accidental CPAN or runtime downloads. Imports of repository modules
# are permitted only when their namespace resolves to scripts/lib.
my %allowed_module = map { $_ => 1 } qw(
  strict warnings lib constant utf8
  Config Cwd Digest::SHA Encode Errno File::Basename File::Copy File::Find File::Path File::Spec File::Temp
  FindBin Getopt::Long HTTP::Tiny IO::Select IO::Socket::INET IPC::Open3
  JSON::PP POSIX Symbol Test::More
);
my %maintained_module = map {
    my $module = $_;
    $module =~ s{\Ascripts/lib/}{};
    $module =~ s{\.pm\z}{};
    $module =~ s{/}{::}g;
    $module => 1;
} @perl_modules;

my @dangerous = (
    [ qr/(?<![\w:])system\s*\(/,             'system invocation' ],
    [ qr/(?<![\w:])exec\s*\(/,               'exec invocation' ],
    [ qr/(?<![\w:])qx\s*[\(\{\/]/,           'qx invocation' ],
    [ qr/(?<![\w:])eval\s+["'\$]/,           'string eval' ],
    [ qr/(?<![\w:])(?:tempnam|tmpnam)\s*\(/, 'insecure temporary-name helper' ],
    [ qr/(?<![\w:])chmod\s+0?777\b/,         'world-writable chmod' ],
    [ qr/(?<![\w:])chmod\s+0?666\b/,         'world-writable chmod' ],
    [
        qr/\bopen\s*\(?\s*[^,\n]+,\s*["'][^"']*[^"'\-]\|["']/,
        'single-scalar pipe open'
    ],
);

for my $relative (@scripts) {
    my $path = File::Spec->catfile( $root, split m{/}, $relative );
    ok( -f $path, "$relative exists" );
    my $text = read_file($path);
    next unless defined $text;

    my $line = first_line($path);
    like(
        $line,
        qr/\A#!\/usr\/bin\/env perl\b/,
        "$relative uses a portable Perl shebang"
    ) if defined $line && $relative !~ /\.pm\z/;
    like( $text, qr/^\s*use\s+strict\s*;/m,   "$relative enables strict" );
    like( $text, qr/^\s*use\s+warnings\s*;/m, "$relative enables warnings" );
    unlike(
        $text,
        qr/^\s*no\s+(?:strict|warnings)\b/m,
        "$relative never disables strict or warnings"
    );
    unlike(
        $text,
        qr/^#!.*\b(?:python|pypy)[0-9.]*\b/im,
        "$relative has no Python shebang"
    );

    if (
        $^O ne 'MSWin32'
        && (   $relative =~ m{\A(?:tools/scripts|android/tools|i18n)/}
            || $relative eq 'scripts/validate-agent-knowledge.pl'
            || $relative eq 'tests/interop/check-vectors.pl' )
      )
    {
        ok( -x $path, "$relative is executable" );
    }

    while ( $text =~ /^\s*(?:use|require)\s+([A-Za-z_][A-Za-z0-9_:]*)/mg ) {
        my $module = $1;
        ok( $allowed_module{$module} || $maintained_module{$module},
            "$relative uses core or maintained module $module" );
    }

    for my $rule (@dangerous) {
        my ( $pattern, $description ) = @{$rule};
        unlike( $text, $pattern, "$relative avoids $description" );
    }
}

my $tools_cmake =
  read_file( File::Spec->catfile( $root, 'tools', 'CMakeLists.txt' ) );
like(
    $tools_cmake,
qr/install\s*\(\s*PROGRAMS\s+"\$\{CMAKE_CURRENT_SOURCE_DIR\}\/scripts\/i2pdctl"/s,
    'CMake installs i2pdctl as a program'
);
like(
    $tools_cmake,
    qr/find_package\s*\(\s*Perl\s*\)/,
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
    return () unless $text =~ $pattern;
    my $block = $1;
    my $operand =
      qr/"\$\{(CMAKE_CURRENT_SOURCE_DIR|PROJECT_ROOT)\}\/([^"]+)"/;
    my @entries;
    while ( $block =~ /$operand/g ) {
        my ( $anchor, $tail ) = ( $1, $2 );
        my $relative =
          $anchor eq 'CMAKE_CURRENT_SOURCE_DIR' ? "tools/$tail" : $tail;

        # The critic command also names its profile; only script
        # operands belong to the inventory comparison.
        push @entries, $relative
          if $relative =~ /\.(?:pl|pm|t)\z/ || $relative =~ m{/i2pdctl\z};
    }
    return @entries;
}

my @expected_syntax = grep { $_ ne 'tools/scripts/i2pdctl' && !/\.pm\z/ } @scripts;
my @syntax_entries = cmake_list_entries( $tools_cmake,
    qr/set\(I2PD_PERL_SYNTAX_SCRIPTS(.*?)\)\n/s );
is_deeply(
    [ sort @syntax_entries ],
    [ sort @expected_syntax ],
    'the CMake syntax sweep registers every maintained script except i2pdctl'
);

my @critic_entries =
  cmake_list_entries( $tools_cmake, qr/add_test\(NAME perl-critic(.*?)\)\n/s );
is_deeply(
    [ sort @critic_entries ],
    [ sort grep { !/\.pm\z/ } @scripts ],
    'the optional Perl::Critic target registers every maintained script'
);
my @cmake_modules = cmake_list_entries($tools_cmake,
    qr/set\(I2PD_AGENT_KNOWLEDGE_MODULES(.*?)\)\n/s);
is_deeply([sort @cmake_modules], [sort @perl_modules],
    'explicit CMake module inventory matches recursive source discovery');
like($tools_cmake,
    qr/list\(APPEND I2PD_PERL_SYNTAX_SCRIPTS \$\{I2PD_AGENT_KNOWLEDGE_MODULES\}\)/,
    'validator modules participate in syntax coverage');
like($tools_cmake,
    qr/add_test\(NAME perl-critic.*?\$\{I2PD_AGENT_KNOWLEDGE_MODULES\}/s,
    'validator modules participate in optional style coverage');

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
for my $relative (@consumers) {
    my $path = File::Spec->catfile( $root, split m{/}, $relative );
    my $text = read_file($path);
    ok( -f $path, "$relative exists" );
    next unless defined $text;
    if ( $comment_only_consumer{$relative} ) {
        $text = join "\n", grep { !/^\s*#/ } split /\n/, $text;
    }
    unlike(
        $text,
qr/(?:\bpython[0-9.]*\b|\bpip[0-9.]*\b|\bpy\s+-3\b|\bPYTHON(?:PATH|HOME)\b|\b(?:virtualenv|venv|tox|poetry)\b)/i,
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
F<i18n/>, F<scripts/> and F<tests/agent_knowledge_validator/>. Validator
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
