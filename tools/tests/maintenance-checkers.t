#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: maintenance checkers.
#
# Negative fixtures for the maintained static checkers. A checker that has
# silently stopped detecting a broken link, an unlisted page or a missing JNI
# definition must fail this suite. Every fixture lives in a File::Temp tree;
# the real repository is never modified and no network or compiler is used.
#

use strict;
use warnings;
use 5.016;

use File::Basename qw(dirname);
use File::Path     qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use FindBin    qw($Bin);
use IO::Select;
use IPC::Open3 qw(open3);
use Symbol     qw(gensym);
use Test::More;

my $root = File::Spec->rel2abs(
    File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir ) );

sub write_file {
    my ( $path, $contents ) = @_;
    # Fixtures are written into a fresh File::Temp tree; create the parent
    # directory so a nested layout such as res/img/ can be reproduced.
    my ( undef, $dir )      = File::Spec->splitpath($path);
    make_path($dir) unless -d $dir;
    open my $fh, '>', $path or die "open $path: $!\n";
    print {$fh} $contents;
    close $fh or die "close $path: $!\n";
}

sub run_checker {
    my ( $script, @args ) = @_;
    my $error = gensym;
    my $pid   = open3( my $input, my $output, $error, $^X, $script, @args );
    close $input;
    # Drain both pipes until EOF. Reading only STDOUT would let a chatty
    # checker block on a full STDERR pipe, and the 30 s wait keeps a hung
    # checker from stalling the whole suite.
    my $select = IO::Select->new( $output, $error );
    my $text   = '';
    while ( my @ready = $select->can_read(30) ) {
        last unless @ready;
        for my $handle (@ready) {
            my $buffer;
            my $read = sysread( $handle, $buffer, 8192 );
            if ( !defined $read ) {
                $select->remove($handle);
                next;
            }
            if ( $read == 0 ) {
                $select->remove($handle);
                next;
            }
            $text .= $buffer;
        }
        last unless $select->count;
    }
    waitpid( $pid, 0 );
    return ( $? >> 8, $text );
}

my $link_checker =
  File::Spec->catfile( $root, 'tools', 'scripts', 'check-doc-links.pl' );
my $nav_checker =
  File::Spec->catfile( $root, 'tools', 'scripts', 'check-doc-nav.pl' );
my $jni_checker = File::Spec->catfile( $root, 'android', 'tools',
    'check-android-jni-symbols.pl' );

# --- check-doc-links.pl ----------------------------------------------------
{
    my $tmp  = tempdir( CLEANUP => 1 );
    my $docs = File::Spec->catdir( $tmp, 'docs' );
    write_file(
        File::Spec->catfile( $docs, 'index.md' ),
        "# Home\n\n[broken](missing.md)\n"
    );
    my ( $code, $out ) = run_checker( $link_checker, $docs );
    is( $code, 1, 'doc link checker rejects a missing relative target' );
    like(
        $out,
        qr/Missing relative documentation links/,
        'missing-link diagnostic is emitted'
    );

    write_file( File::Spec->catfile( $docs, 'missing.md' ), "# Present\n" );
    ( $code, $out ) = run_checker( $link_checker, $docs );
    is( $code, 0, 'doc link checker accepts the repaired tree' );
}

# --- check-doc-nav.pl ------------------------------------------------------
{
    my $tmp  = tempdir( CLEANUP => 1 );
    my $docs = File::Spec->catdir( $tmp, 'docs' );
    write_file( File::Spec->catfile( $docs, 'mkdocs.yml' ),
        "nav:\n  - Home: index.md\n" );
    write_file( File::Spec->catfile( $docs, 'index.md' ),        "# Home\n" );
    write_file( File::Spec->catfile( $docs, 'extra.md' ),        "# Extra\n" );
    write_file( File::Spec->catfile( $docs, 'img', 'logo.png' ), '' );
    write_file( File::Spec->catfile( $docs, 'img', 'favicon.ico' ), '' );

    my ( $code, $out ) = run_checker( $nav_checker, $tmp );
    is( $code, 1, 'doc navigation checker rejects an unlisted page' );
    like( $out, qr/unlisted Markdown/, 'unlisted-page diagnostic is emitted' );

    write_file(
        File::Spec->catfile( $docs, 'mkdocs.yml' ),
        "nav:\n  - Home: index.md\n  - Extra: extra.md\n"
    );
    ( $code, $out ) = run_checker( $nav_checker, $tmp );
    is( $code, 0, 'doc navigation checker accepts a listed page' );
}

# --- check-android-jni-symbols.pl ------------------------------------------
{
    my $tmp  = tempdir( CLEANUP => 1 );
    my $java = File::Spec->catfile(
        $tmp,     'android', 'app', 'src',
        'main',   'java',    'org', 'openi2pd',
        'router', 'I2PD_JNI.java'
    );
    my $header = File::Spec->catfile( $tmp, 'android', 'app', 'src', 'main',
        'cpp', 'org_openi2pd_router_I2PD_JNI.h' );
    my $native = File::Spec->catfile( $tmp, 'android', 'app', 'src', 'main',
        'cpp', 'i2pd_android.cpp' );

    write_file( $java,
            "package org.openi2pd.router;\n"
          . "final class I2PD_JNI {\n"
          . "    public static native void probeOne();\n"
          . "}\n" );
    write_file( $header,
            "JNIEXPORT void JNICALL "
          . "Java_org_openi2pd_router_I2PD_1JNI_probeOne (JNIEnv *, jclass) noexcept;\n"
    );
    write_file( $native,
            "JNIEXPORT void JNICALL "
          . "Java_org_openi2pd_router_I2PD_1JNI_probeOne (JNIEnv *, jclass) noexcept {}\n"
    );

    my ( $code, $out ) = run_checker( $jni_checker, '--root', $tmp );
    is( $code, 0, 'JNI checker accepts matching declarations and definitions' );

    write_file( $native, "// no definition\n" );
    ( $code, $out ) = run_checker( $jni_checker, '--root', $tmp );
    is( $code, 1, 'JNI checker rejects a missing definition' );
    like(
        $out,
        qr/missing C\+\+ definition/,
        'missing-definition diagnostic is emitted'
    );

    write_file( $native,
            "JNIEXPORT void JNICALL "
          . "Java_org_openi2pd_router_I2PD_1JNI_probeTwo (JNIEnv *, jclass) noexcept {}\n"
    );
    ( $code, $out ) = run_checker( $jni_checker, '--root', $tmp );
    is( $code, 1, 'JNI checker rejects an extra definition' );
    like(
        $out,
        qr/has no Java native declaration/,
        'extra-definition diagnostic is emitted'
    );
}

done_testing();

=head1 NAME

maintenance-checkers.t - negative fixtures for the static maintenance checks

=head1 SYNOPSIS

    prove -v tools/tests/maintenance-checkers.t

=head1 DESCRIPTION

Test::More suite that proves the static checkers still detect the defects they
exist for. Every fixture is written into a private File::Temp tree, so the
repository is never modified and the assertions do not depend on the current
documentation, Android or JNI state.

Covered checks:

=over 4

=item *

F<tools/scripts/check-doc-links.pl> must reject a relative link to a missing
page (exit 1 plus the expected diagnostic) and accept the tree once the
target exists;

=item *

F<tools/scripts/check-doc-nav.pl> must reject a Markdown page that is not in
the navigation and accept it after it is listed;

=item *

F<android/tools/check-android-jni-symbols.pl> must accept matching
declarations, reject a Java native method without a C++ definition, and
reject a C++ definition without a Java declaration.

=back

=head1 OUTPUT AND EXIT STATUS

TAP on STDOUT; the process exits non-zero when an assertion fails. Each
checker runs as a child Perl process, and its exit code and combined
STDOUT/STDERR are captured by C<run_checker>. The exit code is the child's
status byte, C<<< $? >> 8 >>>, which is 0 for a child that terminated on a
signal, so a crashing checker can look like a successful one; the diagnostic
assertion is what catches that case in practice.

=head1 DEPENDENCIES

Core Perl with Test::More, File::Temp, File::Path, File::Spec, File::Basename,
FindBin, IO::Select, IPC::Open3 and Symbol. No compiler, build or network
access is used; the only executed programs are the checkers under test.

=head1 PORTABILITY

Uses C<fork>-based C<open3> and POSIX wait semantics through IPC::Open3 and
C<waitpid>, which the OpenBSD base interpreter provides. The fixture trees are
removed through C<CLEANUP> even when an assertion fails.
