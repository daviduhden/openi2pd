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
use IPC::Open3 qw(open3);
use Test::More;

my $root = File::Spec->rel2abs(
    File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir ) );

sub write_file {
    my ( $path, $contents ) = @_;

    # Fixtures are written into a fresh File::Temp tree; create the parent
    # directory so a nested layout such as res/img/ can be reproduced.
    my ( undef, $dir ) = File::Spec->splitpath($path);
    make_path($dir) unless -d $dir;
    open my $fh, '>', $path or die "open $path: $!\n";
    print {$fh} $contents;
    close $fh or die "close $path: $!\n";
}

sub run_checker {
    my ( $script, @args ) = @_;
    my $text = '';

    # STDERR is merged into STDOUT by passing a false error handle:
    # every checker writes its diagnostics to STDERR and its summary to
    # STDOUT, and the assertions below do not care which stream carried
    # the text. A single stream is also the only portable design here:
    # Win32 select() accepts sockets only, so IO::Select on a pipe never
    # becomes readable there and the previous two-pipe drain silently
    # returned an empty capture on Windows.
    my $pid = open3( my $input, my $output, undef, $^X, $script, @args );
    close $input;
    binmode $output;

    # Read to EOF. A checker that never exits is bounded by the CTest
    # TIMEOUT registered for this suite; the fixture checkers are small
    # and synchronous, so a readiness-based deadline would add a
    # platform difference without protecting anything else.
    while ( my $chunk = <$output> ) {
        $text .= $chunk;
    }
    close $output;
    waitpid( $pid, 0 );
    return ( $? >> 8, $text );
}

my $link_checker =
  File::Spec->catfile( $root, 'tools', 'scripts', 'check-doc-links.pl' );
my $nav_checker =
  File::Spec->catfile( $root, 'tools', 'scripts', 'check-doc-nav.pl' );
my $jni_checker = File::Spec->catfile( $root, 'android', 'tools',
    'check-android-jni-symbols.pl' );
my $locale_checker = File::Spec->catfile( $root, 'android', 'tools',
    'check-android-locales.pl' );
my $javadoc_checker = File::Spec->catfile( $root, 'android', 'tools',
    'check-android-javadoc.pl' );

# --- check-android-locales.pl plural categories ---------------------------
{
    my $tmp     = tempdir( CLEANUP => 1 );
    my $android = File::Spec->catdir( $tmp, 'android' );
    my $res = File::Spec->catdir( $android, 'app', 'src', 'main', 'res' );
    my %dirs = (
        en => 'values', es => 'values-es', ru => 'values-ru',
        'zh-CN' => 'values-zh-rCN'
    );
    my %quantities = (
        en => [qw(one other)], es => [qw(one other)],
        ru => [qw(one few many other)], 'zh-CN' => [qw(other)]
    );
    for my $tag (qw(en es ru zh-CN)) {
        my $items = join '', map {
            qq{<item quantity="$_">%1\$d items</item>}
        } @{ $quantities{$tag} };
        my $xml =
            qq{<resources><string name="probe">%1\$d items</string>}
          . qq{<plurals name="probe">$items</plurals></resources>};
        write_file( File::Spec->catfile( $res, $dirs{$tag}, 'strings.xml' ),
            $xml );
    }
    write_file(
        File::Spec->catfile( $res, 'xml', 'locales_config.xml' ),
        '<locale-config><locale android:name="en" />'
          . '<locale android:name="es" /><locale android:name="ru" />'
          . '<locale android:name="zh-CN" /></locale-config>'
    );
    write_file(
        File::Spec->catfile( $res, 'values', 'preferences_i2pd_options.xml' ),
        '<resources>'
          . join( '', map {
            qq{<string-array name="$_" translatable="false" />}
          } qw(pref_language_values pref_bandwidth_values pref_loglevel_values) )
          . '</resources>'
    );
    write_file(
        File::Spec->catfile( $android, 'app', 'src', 'main',
            'AndroidManifest.xml' ),
        '<manifest android:localeConfig="@xml/locales_config">'
          . '<service android:name="AppLocalesMetadataHolderService" />'
          . '</manifest>'
    );
    write_file( File::Spec->catfile( $android, 'app', 'build.gradle' ),
        'androidResources { localeFilters += ["en", "es", "ru", "zh"] }' );
    make_path( File::Spec->catdir( $android, 'fastlane', 'metadata',
        'android', $_ ) ) for qw(en-US es-ES ru-RU zh-CN);

    my ( $code, $out ) = run_checker( $locale_checker, '--root', $android );
    is( $code, 0, 'locale checker accepts other-only Chinese plurals' );

    my $zh_path = File::Spec->catfile( $res, $dirs{'zh-CN'}, 'strings.xml' );
    write_file( $zh_path,
        '<resources><string name="probe">%1$d items</string>'
          . '<plurals name="probe"><item quantity="zero">%1$d items'
          . '</item></plurals></resources>' );
    ( $code, $out ) = run_checker( $locale_checker, '--root', $android );
    is( $code, 1, 'locale checker rejects Chinese catalog missing other' );
    like( $out, qr/missing zh-CN plurals probe quantity=other/,
        'Chinese plural category failure is explicit' );

    write_file( $zh_path,
        '<resources><string name="probe">%1$d items</string>'
          . '<plurals name="probe"><item quantity="one">%1$d item'
          . '</item><item quantity="other">%1$d items</item>'
          . '</plurals></resources>' );
    ( $code, $out ) = run_checker( $locale_checker, '--root', $android );
    is( $code, 1, 'locale checker rejects unused Chinese one quantity' );
    like( $out, qr/unsupported zh-CN plurals probe quantity=one/,
        'unused Chinese quantity diagnostic is explicit' );
}

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

# --- check-android-javadoc.pl ----------------------------------------------
{
    my $tmp  = tempdir( CLEANUP => 1 );
    my $android = File::Spec->catdir( $tmp, 'android' );
    my $java = File::Spec->catfile( $android, 'app', 'src', 'main',
        'java', 'org', 'openi2pd', 'router', 'Probe.java' );

    # The comment forms the manual Gradle Javadoc lane rejects with
    # -Xdoclint:all -Werror: a raw placeholder, a JDK-internal block tag,
    # an inline tag used as a block tag and a mismatched @param name.
    write_file( $java,
            "package org.openi2pd.router;\n"
          . "class Probe {\n"
          . "    /**\n"
          . "     * Keeps the historic `TORRENT-DETAIL <tunnel> <id>` line.\n"
          . "     */\n"
          . "    static String page(int tunnel) {\n"
          . "        return \"x\";\n"
          . "    }\n"
          . "}\n" );
    my ( $code, $out ) = run_checker( $javadoc_checker, '--root', $android );
    is( $code, 1, 'Javadoc checker rejects a raw angle-bracket placeholder' );
    like(
        $out,
        qr/<tunnel> is parsed as HTML/,
        'placeholder diagnostic names the tag'
    );

    write_file( $java,
            "package org.openi2pd.router;\n"
          . "class Probe {\n"
          . "    /**\n"
          . "     * Keeps the historic {\@code TORRENT-DETAIL <tunnel> <id>} line.\n"
          . "     */\n"
          . "    static String page(int tunnel) {\n"
          . "        return \"x\";\n"
          . "    }\n"
          . "}\n" );
    ( $code, $out ) = run_checker( $javadoc_checker, '--root', $android );
    is( $code, 0, 'Javadoc checker accepts an escaped placeholder' );

    write_file( $java,
            "package org.openi2pd.router;\n"
          . "class Probe {\n"
          . "    /**\n"
          . "     * Probe.\n"
          . "     *\n"
          . "     * \@param value accepted value\n"
          . "     * \@implNote Runs on the calling thread.\n"
          . "     */\n"
          . "    static void note(String value) {\n"
          . "    }\n"
          . "}\n" );
    ( $code, $out ) = run_checker( $javadoc_checker, '--root', $android );
    is( $code, 1, 'Javadoc checker rejects an unregistered block tag' );
    like(
        $out,
        qr/unknown tag \@implNote/,
        'unregistered-tag diagnostic names the tag'
    );

    write_file( $java,
            "package org.openi2pd.router;\n"
          . "class Probe {\n"
          . "    /**\n"
          . "     * Probe.\n"
          . "     *\n"
          . "     * \@param data source string\n"
          . "     */\n"
          . "    static void decode(String url) {\n"
          . "    }\n"
          . "}\n" );
    ( $code, $out ) = run_checker( $javadoc_checker, '--root', $android );
    is( $code, 1, 'Javadoc checker rejects a mismatched @param name' );
    like( $out, qr/\@param data is not a parameter/,
        'mismatched-parameter diagnostic is emitted' );
}

# --- CRLF catalogs and nested documentation paths --------------------------
# A CRLF checkout is the failure mode a Windows runner without the
# repository .gitattributes rule reproduces, and a nested documentation page
# is where a native separator leaks into a repository-relative comparison.
{
    my $tmp     = tempdir( CLEANUP => 1 );
    my $android = File::Spec->catdir( $tmp, 'android' );
    my $res     = File::Spec->catdir( $android, 'app', 'src', 'main', 'res' );
    my %dirs = (
        en => 'values', es => 'values-es', ru => 'values-ru',
        'zh-CN' => 'values-zh-rCN'
    );
    my %quantities = (
        en      => [qw(one other)],
        es      => [qw(one other)],
        ru      => [qw(one few many other)],
        'zh-CN' => [qw(other)]
    );
    for my $tag (qw(en es ru zh-CN)) {
        my $items = join '', map {
            qq{<item quantity="$_">%1\$d items</item>}
        } @{ $quantities{$tag} };
        my $xml =
            qq{<resources><string name="probe">%1\$d items</string>}
          . qq{<plurals name="probe">$items</plurals></resources>};
        write_file( File::Spec->catfile( $res, $dirs{$tag}, 'strings.xml' ),
            $xml );
    }
    write_file(
        File::Spec->catfile( $res, 'xml', 'locales_config.xml' ),
        '<locale-config><locale android:name="en" />'
          . '<locale android:name="es" /><locale android:name="ru" />'
          . '<locale android:name="zh-CN" /></locale-config>'
    );
    write_file(
        File::Spec->catfile( $res, 'values', 'preferences_i2pd_options.xml' ),
        '<resources>'
          . join( '', map {
            qq{<string-array name="$_" translatable="false" />}
          } qw(pref_language_values pref_bandwidth_values pref_loglevel_values) )
          . '</resources>'
    );
    write_file(
        File::Spec->catfile( $android, 'app', 'src', 'main',
            'AndroidManifest.xml' ),
        '<manifest android:localeConfig="@xml/locales_config">'
          . '<service android:name="AppLocalesMetadataHolderService" />'
          . '</manifest>'
    );
    write_file( File::Spec->catfile( $android, 'app', 'build.gradle' ),
        'androidResources { localeFilters += ["en", "es", "ru", "zh"] }' );
    make_path( File::Spec->catdir( $android, 'fastlane', 'metadata',
        'android', $_ ) ) for qw(en-US es-ES ru-RU zh-CN);

    my $ru_path = File::Spec->catfile( $res, $dirs{ru}, 'strings.xml' );
    my ( $code, $out ) = run_checker( $locale_checker, '--root', $android );
    is( $code, 0, 'CRLF fixture baseline is accepted with LF endings' );

    # Byte-exact CRLF: text mode would translate on Windows and hide the
    # very defect under test.
    open my $crlf, '>:raw', $ru_path or die "open $ru_path: $!\n";
    print {$crlf}
      "<resources>\r\n<plurals name=\"probe\">\r\n<item quantity=\"one\">"
      . "%1\$d item</item>\r\n<item quantity=\"other\">%1\$d items</item>\r\n"
      . "</plurals>\r\n</resources>\r\n";
    close $crlf;
    ( $code, $out ) = run_checker( $locale_checker, '--root', $android );
    is( $code, 1, 'locale checker rejects a CRLF catalog' );
    like( $out, qr/CRLF line ending in .*strings\.xml:1/,
        'CRLF diagnostic names the file and line' );
    like( $out, qr{\Q$dirs{ru}\E}, 'CRLF diagnostic names the locale' );
}

# --- nested documentation pages -------------------------------------------
{
    my $tmp  = tempdir( CLEANUP => 1 );
    my $docs = File::Spec->catdir( $tmp, 'docs' );
    write_file( File::Spec->catfile( $docs, 'index.md' ),
        "# Home\n\n[nested](sub/page.md)\n" );
    write_file( File::Spec->catfile( $docs, 'sub', 'page.md' ),
        "# Page\n\n[home](../index.md)\n" );
    write_file( File::Spec->catfile( $docs, 'mkdocs.yml' ),
        "nav:\n  - Home: index.md\n  - Page: sub/page.md\n" );

    my ( $code, $out ) = run_checker( $link_checker, $docs );
    is( $code, 0, 'doc link checker resolves nested and parent links' );

    # Three levels up leaves the repository root, which is the parent of
    # the documentation directory.
    write_file( File::Spec->catfile( $docs, 'sub', 'page.md' ),
        "# Page\n\n[out](../../../outside.md)\n" );
    ( $code, $out ) = run_checker( $link_checker, $docs );
    is( $code, 1, 'doc link checker rejects a target above the repository' );
    like( $out, qr/\(escapes repository\)/,
        'escape diagnostic is emitted for a parent-of-repository link' );
}

# --- nested navigation pages ----------------------------------------------
{
    my $tmp  = tempdir( CLEANUP => 1 );
    my $docs = File::Spec->catdir( $tmp, 'docs' );
    write_file( File::Spec->catfile( $docs, 'mkdocs.yml' ),
        "nav:\n  - Home: index.md\n" );
    write_file( File::Spec->catfile( $docs, 'index.md' ), "# Home\n" );
    write_file( File::Spec->catfile( $docs, 'sub', 'page.md' ),
        "# Nested\n" );
    write_file( File::Spec->catfile( $docs, 'img', 'logo.png' ), '' );
    write_file( File::Spec->catfile( $docs, 'img', 'favicon.ico' ), '' );

    my ( $code, $out ) = run_checker( $nav_checker, $tmp );
    is( $code, 1, 'doc navigation checker rejects an unlisted nested page' );
    like( $out, qr{unlisted Markdown: sub/page\.md},
        'nested page is reported with / separators' );

    write_file( File::Spec->catfile( $docs, 'mkdocs.yml' ),
        "nav:\n  - Home: index.md\n  - Nested: sub/page.md\n" );
    ( $code, $out ) = run_checker( $nav_checker, $tmp );
    is( $code, 0, 'doc navigation checker accepts a listed nested page' );
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
target exists; a nested page's parent link must resolve and a link that
leaves the repository root must be reported as escaping it;

=item *

F<tools/scripts/check-doc-nav.pl> must reject a Markdown page that is not in
the navigation and accept it after it is listed, including a page in a
nested directory (reported with C</> separators on every platform);

=item *

F<android/tools/check-android-locales.pl> must reject a catalog with CRLF
line endings and name the file and line, so a converted checkout is
distinguishable from a catalog defect;

=item *

F<android/tools/check-android-javadoc.pl> must reject a raw
angle-bracket placeholder, an unregistered C<@implNote> tag and a
documented parameter the declaration does not have, and must accept the
same placeholder inside C<{@code}>.

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

The child's STDERR is merged into the captured STDOUT stream, because the
diagnostics under test are written to STDERR while the summaries go to
STDOUT, and Win32 C<select()> only accepts sockets: draining two pipes with
C<IO::Select> silently returned an empty capture on Windows and reported
every diagnostic assertion as missing.

=head1 DEPENDENCIES

Core Perl with Test::More, File::Temp, File::Path, File::Spec, File::Basename,
FindBin and IPC::Open3. No compiler, build or network access is used; the only
executed programs are the checkers under test.

=head1 PORTABILITY

Uses C<fork>-based C<open3> and POSIX wait semantics through IPC::Open3 and
C<waitpid>, which the OpenBSD base interpreter provides. The capture is a
single stream so no readiness wait on a pipe is needed; a checker that never
exits is bounded by the CTest timeout of this suite. The fixture trees are
removed through C<CLEANUP> even when an assertion fails.
