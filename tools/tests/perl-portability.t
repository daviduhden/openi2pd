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

use File::Basename qw(basename);
use File::Find qw(find);
use File::Spec;
use FindBin qw($Bin);
use Test::More;

my $tools = File::Spec->rel2abs(File::Spec->catdir($Bin, File::Spec->updir));
my $root = File::Spec->rel2abs(File::Spec->catdir($tools, File::Spec->updir));

sub read_file {
    my ($path) = @_;
    open my $fh, '<', $path or return undef;
    local $/;
    my $text = <$fh>;
    close $fh;
    return defined $text ? $text : '';
}

sub first_line {
    my ($path) = @_;
    open my $fh, '<', $path or return undef;
    # A shebang is far shorter than this; do not pull a large binary asset
    # into memory merely because the inventory scan visits it.
    my $line = '';
    read($fh, $line, 512);
    close $fh;
    $line =~ s/^\x{FEFF}//;
    return $line;
}

my %skip_directory = map { $_ => 1 } qw(
    .git .cache .gradle .venv __pycache__ build-output dist node_modules
    out target venv
);
my (@python_files, @python_shebang);

find(
    {
        no_chdir => 1,
        preprocess => sub {
            return grep { !$skip_directory{$_} } @_;
        },
        wanted => sub {
            my $path = $File::Find::name;
            return if -d $path;
            my $name = basename($path);
            push @python_files, $path
                if $name =~ /\.(?:py|pyw|pyi|pyx|pxd|ipynb)\z/i
                || $name =~ /\.py\.in\z/i
                || $name =~ /\A(?:SConstruct|SConscript|wscript|pyproject\.toml|setup\.cfg|requirements[^\/]*\.txt)\z/i;

            my $prefix = first_line($path);
            if (defined $prefix) {
                my @head = split /\n/, $prefix;
                my $shebang = join "\n", @head[0 .. ($#head < 2 ? $#head : 2)];
                if ($shebang =~ /^#!.*\b(?:python|pypy)[0-9.]*\b/im
                    || $shebang =~ /^#!.*\b(?:python|pypy)[0-9.]*\b/i) {
                    push @python_shebang, $path;
                }
            }
        },
    },
    $root
);

is_deeply(\@python_files, [], 'no project-maintained Python source files');
is_deeply(\@python_shebang, [], 'no Python shebang in maintained files');

my @scripts = (
    'tools/scripts/i2pdctl',
    'tools/scripts/check-doc-links.pl',
    'tools/scripts/check-doc-nav.pl',
    'tools/scripts/validate-branding.pl',
    'tools/tests/i2pdctl.t',
    'tools/tests/validate-branding.t',
    'tools/tests/perl-portability.t',
    'tests/interop/check-vectors.pl',
    'android/tools/check-android-locales.pl',
);

my @discovered = map { File::Spec->abs2rel($_, $root) }
    grep { -f $_ }
    (glob(File::Spec->catfile($root, 'tools', 'scripts', '*')),
     glob(File::Spec->catfile($root, 'tools', 'tests', '*.t')),
     glob(File::Spec->catfile($root, 'tests', 'interop', '*.pl')),
     glob(File::Spec->catfile($root, 'android', 'tools', '*.pl')));
is_deeply([ sort @discovered ], [ sort @scripts ],
    'maintained Perl inventory matches discovered scripts');

# Modules shipped with the Perl core on OpenBSD/Linux. This also rejects
# accidental CPAN or runtime downloads.
my %allowed_module = map { $_ => 1 } qw(
    strict warnings
    Config Cwd Digest::SHA File::Basename File::Find File::Spec File::Temp
    FindBin Getopt::Long HTTP::Tiny IO::Select IO::Socket::INET IPC::Open3
    JSON::PP POSIX Symbol Test::More
);

my @dangerous = (
    [qr/(?<![\w:])system\s*\(/, 'system invocation'],
    [qr/(?<![\w:])exec\s*\(/, 'exec invocation'],
    [qr/(?<![\w:])qx\s*[\(\{\/]/, 'qx invocation'],
    [qr/(?<![\w:])eval\s+["'\$]/, 'string eval'],
    [qr/(?<![\w:])(?:tempnam|tmpnam)\s*\(/, 'insecure temporary-name helper'],
    [qr/(?<![\w:])chmod\s+0?777\b/, 'world-writable chmod'],
    [qr/(?<![\w:])chmod\s+0?666\b/, 'world-writable chmod'],
    [qr/\bopen\s*\(?\s*[^,\n]+,\s*["'][^"']*[^"'\-]\|["']/, 'single-scalar pipe open'],
);

for my $relative (@scripts) {
    my $path = File::Spec->catfile($root, split m{/}, $relative);
    ok(-f $path, "$relative exists");
    my $text = read_file($path);
    next unless defined $text;

    my $line = first_line($path);
    like($line, qr/\A#!\/usr\/bin\/env perl\b/,
        "$relative uses a portable Perl shebang") if defined $line;
    like($text, qr/^\s*use\s+strict\s*;/m, "$relative enables strict");
    like($text, qr/^\s*use\s+warnings\s*;/m, "$relative enables warnings");
    unlike($text, qr/^\s*no\s+(?:strict|warnings)\b/m,
        "$relative never disables strict or warnings");
    unlike($text, qr/^#!.*\b(?:python|pypy)[0-9.]*\b/im,
        "$relative has no Python shebang");

    if ($^O ne 'MSWin32'
        && ($relative =~ m{\A(?:tools/scripts|android/tools)/}
            || $relative eq 'tests/interop/check-vectors.pl')) {
        ok(-x $path, "$relative is executable");
    }

    while ($text =~ /^\s*(?:use|require)\s+([A-Za-z_][A-Za-z0-9_:]*)/mg) {
        my $module = $1;
        ok($allowed_module{$module}, "$relative uses core module $module");
    }

    for my $rule (@dangerous) {
        my ($pattern, $description) = @{$rule};
        unlike($text, $pattern, "$relative avoids $description");
    }
}

my $tools_cmake = read_file(File::Spec->catfile($root, 'tools', 'CMakeLists.txt'));
like($tools_cmake, qr/install\s*\(\s*PROGRAMS\s+"\$\{CMAKE_CURRENT_SOURCE_DIR\}\/scripts\/i2pdctl"/s,
    'CMake installs i2pdctl as a program');
like($tools_cmake, qr/find_package\s*\(\s*Perl\s*\)/,
    'CMake discovers the Perl interpreter for static checks');
ok(-f File::Spec->catfile($root, '.perlcriticrc'),
    'optional Perl::Critic profile is present');

# Build consumers must not regress to Python. Documentation that records the
# historical migration is intentionally not included in this list.
my @consumers = (
    'tools/CMakeLists.txt',
    'tests/CMakeLists.txt',
    'build/CMakeLists.txt',
    'Makefile',
    'tests/Makefile',
    '.github/workflows/build.yml',
    '.github/workflows/build-windows.yml',
    '.github/workflows/build-openbsd.yml',
    '.github/workflows/fuzz.yml',
    '.github/workflows/packaging.yml',
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
);
for my $relative (@consumers) {
    my $path = File::Spec->catfile($root, split m{/}, $relative);
    my $text = read_file($path);
    ok(-f $path, "$relative exists");
    next unless defined $text;
    unlike($text,
        qr/(?:\bpython[0-9.]*\b|\bpip[0-9.]*\b|\bpy\s+-3\b|\bPYTHON(?:PATH|HOME)\b|\b(?:virtualenv|venv|tox|poetry)\b)/i,
        "$relative has no Python invocation");
}

done_testing();
