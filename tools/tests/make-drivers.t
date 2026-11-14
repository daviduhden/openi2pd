#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: mock regressions for Makefile command drivers.
#
# Every external build/package command is a recording fixture. These tests
# invoke the shell drivers, never make, CMake, CTest or a real install tool.
# Fixtures and destinations live in a private File::Temp tree.

use 5.016;
use strict;
use warnings;
use Cwd            qw(getcwd);
use Errno          qw(EINTR);
use File::Basename qw(dirname);
use File::Copy     qw(copy);
use File::Path     qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use FindBin    qw($Bin);
use IPC::Open3 qw(open3);
use JSON::PP;
use Test::More;

plan skip_all => 'POSIX make/package shell drivers' if $^O eq 'MSWin32';

my $root      = File::Spec->rel2abs( File::Spec->catdir( $Bin, '..', '..' ) );
my $temporary = tempdir( CLEANUP => 1 );
my $workspace = "$temporary/work space";
my $commands  = "$temporary/command space";
my $log       = "$temporary/commands.jsonl";
make_path( $workspace, $commands );

sub write_file {
    my ( $path, $text ) = @_;
    make_path( dirname($path) );
    open my $file, '>', $path or die "open $path: $!\n";
    print {$file} $text;
    close $file or die "close $path: $!\n";
    return;
}

sub read_file {
    my ($path) = @_;
    open my $file, '<', $path or die "open $path: $!\n";
    local $/ = undef;
    my $text = <$file>;
    close $file or die "close $path: $!\n";
    return $text // '';
}

my $fixture = "$root/tools/tests/fixtures/record-build-command.pl";
die "missing regular mock fixture\n" unless -f $fixture && !-l $fixture;
for my $name (qw(cmake ctest git dpkg-buildflags install cp)) {
    my $path = "$commands/$name";
    copy( $fixture, $path ) or die "copy recording fixture: $!\n";
    chmod 0700, $path or die "chmod $path: $!\n";
}

# Merge child diagnostics into stdout: selecting pipes is not portable.
# Every invoked program is our fixture, so no build process can escape.
sub invoke {
    my ( $script, $environment, @args ) = @_;
    write_file( $log, '' );
    local %ENV = %ENV;
    $ENV{PATH}     = "$commands:$ENV{PATH}";
    $ENV{MOCK_LOG} = $log;
    delete @ENV{
        qw(MOCK_FAIL DEB_BUILD_OPTIONS CMAKE_BUILD_PARALLEL_LEVEL DESTDIR)};
    @ENV{ keys %{$environment} } = values %{$environment};
    my $previous = getcwd();
    chdir $workspace or die "chdir $workspace: $!\n";
    my ( $input, $output );
    my $pid =
      eval { open3( $input, $output, undef, 'sh', "$root/$script", @args ) };
    my $error = $@;
    chdir $previous or die "restore cwd: $!\n";
    die $error unless $pid;
    close $input;
    my $text = do { local $/ = undef; <$output> }
      // '';
    close $output;

    while (1) {
        my $waited = waitpid $pid, 0;
        last if $waited == $pid;
        next if $waited == -1 && $! == EINTR;
        die "wait mock shell: $!\n";
    }
    my $status = $?;
    my $code   = $status & 127 ? 128 + ( $status & 127 ) : $status >> 8;
    my @calls  = map { JSON::PP->new->decode($_) } grep { length } split /\n/,
      read_file($log);
    return ( $code, \@calls, $text );
}

sub root_driver {
    my ( $action, $env, $directory, $prefix, @flags ) = @_;
    return invoke( 'build/make-driver.sh', $env, $action, "$commands/cmake",
        $directory, 'Ninja', "$temporary/stage space",
        $prefix,    @flags );
}

subtest 'declarative graph and original flag argv' => sub {
    my $makefile = read_file("$root/Makefile");
    like(
        $makefile,
        qr/^build: configure$/m,
        'build still configures through its prerequisite'
    );
    like( $makefile, qr/^install: build$/m, 'install still builds first' );
    like( $makefile, qr/^check: test$/m,    'check remains the test alias' );
    like(
        $makefile,
        qr/\$\{MAKE_DRIVER\} configure.*\$\{CMAKE_FLAGS\}\s*$/m,
        'caller passes flags as original trailing argv'
    );
    unlike(
        $makefile,
        qr/^\t.*\b(?:if|case|for|while)\b/m,
        'recipes contain no control flow'
    );
    my ( $code, $calls ) = root_driver(
        'configure', {}, 'build space', '', '-DCMAKE_BUILD_TYPE=Debug',
        '-DVALUE=two words',
        q{-DLITERAL=$(touch should-not-exist)}
    );
    is( $code, 0, 'configure driver accepts argv' );
    is_deeply(
        $calls->[0]{args},
        [
            '-S',                       'build',
            '-B',                       'build space',
            '-G',                       'Ninja',
            '-DCMAKE_BUILD_TYPE=Debug', '-DVALUE=two words',
            q{-DLITERAL=$(touch should-not-exist)}
        ],
        'spaces and metacharacters are literal argv'
    );
    ok( !-e "$workspace/should-not-exist",
        'driver never evals supplied flags' );
};

subtest 'root phase ordering and failures' => sub {
    for my $case (
        [ router => '-DI2PD_BUILD_TUI=OFF', 'i2pd' ],
        [ tui    => '-DI2PD_BUILD_TUI=ON',  'i2pd-tui' ]
      )
    {
        my ( $code, $calls ) =
          root_driver( $case->[0], {}, 'out', '', '-DUSER=last' );
        is( $code, 0, "$case->[0] success" );
        is( $calls->[0]{args}[-2],
            $case->[1], 'target default precedes user flags' );
        is_deeply(
            $calls->[1]{args},
            [ '--build', 'out', '--target', $case->[2] ],
            'only intended native target requested'
        );
    }
    my ( $code, $calls ) = root_driver( 'test', {}, 'out', '' );
    is( $code, 0, 'mock test phase succeeds' );
    is_deeply( [ map { $_->{name} } @{$calls} ],
        [qw(cmake cmake ctest)], 'configure/build/test order retained' );
    ( $code, $calls ) = root_driver( 'tools', {}, 'out', '', '-DUSER=last' );
    is( $code, 0, 'tools phase succeeds' );
    is_deeply(
        [ @{ $calls->[0]{args} }[ -3 .. -1 ] ],
        [ '-DI2PD_BUILD_TOOLS=ON', '-DI2PD_BUILD_TUI=OFF', '-DUSER=last' ],
        'tool defaults precede caller overrides'
    );
    ( $code, $calls ) =
      root_driver( 'router', { MOCK_FAIL => 'cmake' }, 'out', '' );
    is( $code,            23, 'configuration failure propagates' );
    is( scalar @{$calls}, 1,  'failure prevents build' );
    ( $code, $calls ) =
      root_driver( 'build', { CMAKE_BUILD_PARALLEL_LEVEL => '3' }, 'out', '' );
    is( $code,                 0,   'direct build helper succeeds' );
    is( $calls->[0]{parallel}, '3', 'CMake parallel environment survives' );
    is_deeply(
        $calls->[0]{args},
        [ '--build', 'out' ],
        'helper does not force a clean or new configure'
    );
    ( $code, $calls ) = root_driver( 'dist', {}, 'out', '' );
    is_deeply(
        $calls->[0]{args},
        [
            'archive', '--format=tar.gz',
            '-9',      '--prefix=openi2pd/',
            'HEAD',    '-o',
            'openi2pd.tar.gz'
        ],
        'dist stays a Git archive'
    );
};

subtest 'root install staging and guarded clean' => sub {
    my ( $code, $calls ) =
      root_driver( 'install', {}, 'out', '/usr/local space' );
    is( $code, 0, 'prefix install success' );
    is_deeply(
        $calls->[0]{args},
        [ '--install', 'out', '--prefix', '/usr/local space' ],
        'prefix is one operand'
    );
    is(
        $calls->[0]{destdir},
        "$temporary/stage space",
        'staging environment survives'
    );
    ( $code, $calls ) = root_driver( 'install', {}, 'out', '' );
    is_deeply(
        $calls->[0]{args},
        [ '--install', 'out' ],
        'empty prefix preserves configured prefix'
    );
    for my $unsafe ( '', '.', '..', '/' ) {
        ( $code, $calls ) = root_driver( 'clean', {}, $unsafe, '' );
        isnt( $code, 0, 'unsafe cleanup refused' );
        is( scalar @{$calls}, 0, 'unsafe cleanup invokes no external command' );
    }
    ( $code, $calls ) = root_driver( 'clean', {}, 'no-cache', '' );
    isnt( $code, 0, 'missing cache refuses cleanup' );
    is( scalar @{$calls}, 0, 'missing cache invokes no removal command' );
    write_file( "$workspace/build space/CMakeCache.txt", "fixture\n" );
    ( $code, $calls ) = root_driver( 'clean', {}, 'build space', '' );
    is( $code, 0, 'cache-authorized cleanup succeeds' );
    is_deeply(
        $calls->[0]{args},
        [ '-E', 'remove_directory', 'build space' ],
        'cleanup operand preserved'
    );
    write_file( "$workspace/CMakeCache.txt", "source-root decoy\n" );
    write_file( "$temporary/CMakeCache.txt", "ancestor decoy\n" );

    for
      my $alias ( './', '//', 'build space/..', $workspace, $temporary, '../' )
    {
        ( $code, $calls ) = root_driver( 'clean', {}, $alias, '' );
        isnt( $code, 0,
            'physical root/ancestor alias refused despite a cache marker' );
        is( scalar @{$calls}, 0, 'alias cleanup invokes no external command' );
    }
  SKIP: {
        skip 'symlinks unavailable', 2
          unless symlink $workspace, "$workspace/build alias";
        ( $code, $calls ) = root_driver( 'clean', {}, 'build alias', '' );
        isnt( $code, 0, 'symlink alias to source root is refused' );
        is( scalar @{$calls}, 0, 'symlink alias invokes no removal command' );
    }
    write_file( "$temporary/external build/CMakeCache.txt",
        "external fixture\n" );
    ( $code, $calls ) =
      root_driver( 'clean', {}, "$temporary/external build", '' );
    is( $code, 0,
        'external non-ancestor build with a cache remains supported' );
};

subtest 'Debian flags and parallel count' => sub {
    my %flags = (
        MOCK_CFLAGS             => '-O2',
        MOCK_CPPFLAGS           => q{-DNAME="two words"},
        MOCK_CXXFLAGS           => '-O3 -Wall',
        MOCK_LDFLAGS            => '-Wl,-z,relro',
        DEB_BUILD_MAINT_OPTIONS => 'hardening=+all'
    );
    my ( $code, $calls ) =
      invoke( 'debian/build-driver.sh', \%flags, 'configure' );
    is( $code, 0, 'Debian configure succeeds' );
    my $configure = $calls->[-1];
    is( $configure->{name}, 'cmake', 'dpkg flags collected before CMake' );
    ok(
        grep( { $_ eq '-DCMAKE_CXX_FLAGS=-O3 -Wall ' . $flags{MOCK_CPPFLAGS} }
            @{ $configure->{args} } ),
        'compiler flags stay one definition'
    );
    is( $configure->{hardening},
        'hardening=+all', 'maintainer flag environment retained' );
    ( $code, $calls ) =
      invoke( 'debian/build-driver.sh', { MOCK_FAIL => 'dpkg-buildflags' },
        'configure' );
    is( $code, 23, 'dpkg flag failure cannot silently drop hardening' );
    is( scalar @{$calls}, 1, 'flag failure prevents CMake' );

    for my $option ( '', 'nocheck', 'parallel=', 'parallel=4 nocheck' ) {
        ( $code, $calls ) =
          invoke( 'debian/build-driver.sh', { DEB_BUILD_OPTIONS => $option },
            'build' );
        is( $code, 0, "parallel option '$option' accepted" );
        my @expected = ( '--build', 'build-debian', '--parallel' );
        push @expected, '4' if $option =~ /parallel=4/;
        is_deeply( $calls->[0]{args}, \@expected, 'parallel argv preserved' );
    }
    for my $invalid ( 'parallel=0', 'parallel=two', 'parallel=2 parallel=3' ) {
        ( $code, $calls ) =
          invoke( 'debian/build-driver.sh', { DEB_BUILD_OPTIONS => $invalid },
            'build' );
        isnt( $code, 0, 'invalid/ambiguous parallel count refused' );
        is( scalar @{$calls}, 0, 'no build on invalid metadata' );
    }
    ( $code, $calls ) = invoke( 'debian/build-driver.sh', {}, 'install',
        "$workspace/source root" );
    is(
        $calls->[0]{destdir},
        "$workspace/source root/debian/tmp",
        'Debian staging quotes source root'
    );
};

subtest 'prebuilt payload and OpenBSD framework commands' => sub {
    my ( $code, $calls ) = invoke( 'contrib/static/debian/install-payload.sh',
        {}, 'arm64', 'package space' );
    is( $code,            0, 'static install succeeds with mocks' );
    is( scalar @{$calls}, 7, 'original payload command inventory retained' );
    is_deeply(
        $calls->[0]{args},
        [ '-D', '-m', '0755', 'bin/i2pd-arm64', 'package space/usr/bin/i2pd' ],
        'architecture and destination preserved'
    );
    is( $calls->[-1]{name}, 'cp', 'certificate tree copied last' );
    ( $code, $calls ) = invoke(
        'contrib/static/debian/install-payload.sh',
        { MOCK_FAIL => 'install' },
        'arm64', 'pkg'
    );
    is( $code,            23, 'static install failure propagates before copy' );
    is( scalar @{$calls}, 1,  'no remaining payload after failure' );

    for my $phase (
        [ dirs   => '-d',     2 ],
        [ data   => '-m0644', 9 ],
        [ script => '-m0755', 1 ]
      )
    {
        ( $code, $calls ) = invoke(
            'contrib/openbsd/port/install-examples.sh', {},
            $phase->[0],    'source space',
            'prefix space', 'stage space',
            'port space',   "$commands/install",
            $phase->[1]
        );
        is( $code, 0, "OpenBSD $phase->[0] phase succeeds" );
        is( scalar @{$calls},
            $phase->[2], 'framework phase command count retained' );
        is( $calls->[0]{args}[0],
            $phase->[1], 'framework flags preserved as argv' );
    }
    is_deeply(
        $calls->[0]{args},
        [
            '-m0755',
            'source space/contrib/openbsd/i2pd.rc',
            'stage space/etc/rc.d/i2pd'
        ],
        'substituted rc.d source and staged destination retained'
    );
};

done_testing();

__END__

=head1 NAME

make-drivers.t - mock-only command driver regressions

=head1 CONTRACT

Requires Perl core modules and a POSIX shell. Windows skips these POSIX
frontend/package fixtures. All compiler/build/test/install programs are
recording mocks; no real build, package mutation or network operation is
performed. The private temporary tree owns every fixture and log.
Makefile dependencies and flag forwarding are inspected as source text.

=cut
