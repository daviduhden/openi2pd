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
use File::Copy qw(copy);
use File::Find ();
use File::Path     qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use FindBin    qw($Bin);
use IPC::Open3 qw(open3);
use Digest::SHA qw(sha256_hex);
use Errno qw(ECHILD EINTR);
use POSIX qw(WIFEXITED WEXITSTATUS WIFSIGNALED WTERMSIG);
use JSON::PP;
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
    my $waited;
    my $wait_error = 0;
    while (1) {
        $waited = waitpid( $pid, 0 );
        $wait_error = 0 + $!;
        last if $waited == $pid;
        next if $waited == -1 && $! == EINTR;
        last;
    }
    my ( $code, $kind, $detail ) = decode_wait_status(
        $pid, $waited, $?, $wait_error
    );
    $text .= "run_checker: $kind ($detail)\n" if $kind ne 'exit';
    return ( $code, $text, $kind, $detail );
}

sub decode_wait_status {
    my ( $pid, $waited, $status, $wait_error ) = @_;
    return ( 255, 'waitpid-error', $wait_error ) if $waited == -1;
    return ( 255, 'waitpid-error', 0 ) if $waited != $pid;
    return ( 128 + WTERMSIG($status), 'signal', WTERMSIG($status) )
      if WIFSIGNALED($status);
    return ( WEXITSTATUS($status), 'exit', 0 ) if WIFEXITED($status);
    return ( 255, 'waitpid-error', 0 );
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
my $doxygen_checker = File::Spec->catfile( $root, 'tools', 'scripts',
    'check-doxygen-params.pl' );
my $i18n_checker = File::Spec->catfile( $root, 'i18n',
    'check-i18n-catalogs.pl' );
my $shell_checker = File::Spec->catfile(
    $root, 'tools', 'scripts', 'check-shell-policy.pl' );

# Host-language inputs are data fixtures. Never execute their commands;
# only the source-only policy checker runs in the authorized CI suite.
{
    my $tmp = tempdir(CLEANUP => 1);
    my $source = File::Spec->catfile($tmp, 'probe.sh');
    my @accepted = (
        "perl --version\n",
        "perl tools/helper.pl operand\n",
        "perl -c tools/helper.pl\n",
        "perl -cw tools/helper.pl\n",
        "perl -MEncode -I tree tools/helper.pl\n",
        "perl -MFile::Spec tools/helper.pl\n",
        "perl -i.perl tools/helper.pl\n",
        "grep perl settings.conf\n",
        "echo 'perl -e sample documentation'\n",
    );
    for my $sample (@accepted) {
        write_file($source, $sample);
        my ($code, $out) = run_checker($shell_checker, '--root', $tmp);
        is($code, 0, 'host scanner accepts file-based or non-command operands');
    }
    my @rejected = (
        "perl -e 'print 1'\n",
        "perl -we 'print 1'\n",
        "perl -npe 's/a/b/g'\n",
        "perl -MEncode -e 'print 1'\n",
        "/usr/bin/perl -E 'say 1'\n",
        "env MODE=fixture /usr/bin/perl -pe 's/a/b/g'\n",
        "env -S 'perl -we print'\n",
        "env --split-string='perl -e print'\n",
        "command -p perl -e 'print 1'\n",
        "\@perl -e source\n",
        "cmd /c perl -e source\n",
        "call perl -e source\n",
        "\"C:\\Perl\\bin\\perl.exe\" -e source\n",
        "perl -x host.sh\n",
        "perl -M'File::Spec; BEGIN {print 1}' tools/helper.pl\n",
        "perl -M 'File::Spec; BEGIN {print 1}' tools/helper.pl\n",
        "perl <<'PROGRAM'\nprint 1;\nPROGRAM\n",
        "printf source | perl\n",
        "perl -\n",
        "perl -c\n",
        "python3 -c 'print(1)'\n",
        "env /usr/bin/python3 -Ic 'print(1)'\n",
        "perl -e 'secret-inline-marker'\n",
    );
    for my $sample (@rejected) {
        write_file($source, $sample);
        my ($code, $out) = run_checker($shell_checker, '--root', $tmp);
        is($code, 1, 'host scanner rejects inline or standard-input interpreter source');
        like($out, qr/probe[.]sh:1:/, 'host violation identifies the source location');
        unlike($out, qr/secret-inline-marker/, 'host diagnostic never echoes code operands');
    }
    write_file($source, "perl -c tools/helper.pl\n");
    my $make = File::Spec->catfile($tmp, 'Makefile');
    for my $sample (
        '$(PERL) -we source', '$(PERL_EXECUTABLE) -e source',
        '$${PERL} -e source', '$(shell perl -e source)' ) {
        write_file($make, $sample . "\n");
        my ($code, $out) = run_checker($shell_checker, '--root', $tmp);
        is($code, 1, 'Make interpreter variables and command substitution retain source checks');
    }
    write_file($make, '$(PERL) -c tools/helper.pl' . "\n");
    my $cmake = File::Spec->catfile($tmp, 'CMakeLists.txt');
    write_file($cmake, 'execute_process(COMMAND "${PERL_EXECUTABLE}"' . "\n"
        . ' -we "print 1")' . "\n");
    my ($code, $out) = run_checker($shell_checker, '--root', $tmp);
    is($code, 1, 'host scanner covers multiline CMake interpreter arguments');
    write_file($cmake, 'execute_process(COMMAND "${PERL_EXECUTABLE}" -c "${script}")' . "\n");
    my $gradle = File::Spec->catfile($tmp, 'build.gradle');
    write_file($gradle, "commandLine 'perl', '-we', 'print 1'\n");
    ($code, $out) = run_checker($shell_checker, '--root', $tmp);
    is($code, 1, 'Gradle command argument arrays cannot embed interpreter source');
    write_file($gradle, "commandLine 'perl', '-c', 'helper.pl'\n");
    my $markdown = File::Spec->catfile($tmp, 'guide.md');
    write_file($markdown, "Documentation: perl -e is forbidden.\n```perl\nprint 1;\n```\n");
    ($code, $out) = run_checker($shell_checker, '--root', $tmp);
    is($code, 0, 'prose and standalone Perl fences are not host commands');
    write_file($markdown, "```sh\nperl -e 'print 1'\n```\n");
    ($code, $out) = run_checker($shell_checker, '--root', $tmp);
    is($code, 1, 'documentation shell fences obey the same embedding policy');
    like($out, qr/guide[.]md:2:/, 'documentation fence preserves original line numbers');
}

# Crash diagnostics must reject commands that expose process arguments
# or memory. Reproduce only the checker and its workflow inputs.
sub workflow_crash_fixtures {
    my ($fixture_root) = @_;
    my $tmp = tempdir( CLEANUP => 1 );
    for my $relative ( '.github/workflows', '.github/actions', 'tools/scripts/check-workflows.pl' ) {
        File::Find::find( {
            no_chdir => 1,
            wanted => sub {
                return unless -f $File::Find::name;
                my $path = File::Spec->abs2rel( $File::Find::name, $fixture_root );
                my $target = File::Spec->catfile( $tmp, $path );
                make_path( dirname($target) );
                copy( $File::Find::name, $target ) or BAIL_OUT("copy workflow fixture: $!");
                return;
            },
        }, File::Spec->catfile( $fixture_root, $relative ) );
    }
    my $checker = File::Spec->catfile( $tmp, 'tools', 'scripts', 'check-workflows.pl' );
    my ( $code, $out ) = run_checker($checker);
    is( $code, 0, 'current workflow crash commands expose code locations only' );
    my $windows = File::Spec->catfile( $tmp, '.github', 'workflows', 'build-windows.yml' );
    open my $fh, '<', $windows or BAIL_OUT("read workflow fixture: $!");
    my $source = do { local $/ = undef; <$fh> };
    close $fh or BAIL_OUT("close workflow fixture: $!");
    for my $unsafe ( 'kp 40', '.dump /ma unsafe.dmp', 'dv' ) {
        my $candidate = $source;
        $candidate =~ s/kc[ ]40/$unsafe/gx;
        write_file( $windows, $candidate );
        ( $code, $out ) = run_checker($checker);
        is( $code, 1, 'workflow checker rejects memory or argument collection' );
        like( $out, qr/code[ ]locations[ ]only/x, 'crash evidence privacy diagnostic' );
    }
    write_file( $windows, $source );
    my $validation = File::Spec->catfile( $tmp, '.github', 'workflows', 'validation.yml' );
    open my $validation_fh, '<', $validation or BAIL_OUT("read sanitizer fixture: $!");
    my $validation_source = do { local $/ = undef; <$validation_fh> };
    close $validation_fh or BAIL_OUT("close sanitizer fixture: $!");
    for my $missing ( 'build-sanitizers/tests/test-i2ptunnel-stop',
        'sanitizer-evidence/revision.txt', 'sha256sum' ) {
        my $candidate = $validation_source;
        $candidate =~ s/\Q$missing\E/missing-evidence/gx;
        write_file( $validation, $candidate );
        ( $code, $out ) = run_checker($checker);
        is( $code, 1, 'workflow checker rejects missing sanitizer evidence' );
        like( $out, qr/sanitizer[ ].*evidence[ ]missing/x,
            'sanitizer evidence diagnostic identifies the gap' );
    }
    write_file( $validation, $validation_source );
    my $newline_inventory = $validation_source;
    $newline_inventory =~ s/git ls-files -z/git ls-files/g;
    write_file( $validation, $newline_inventory );
    ( $code, $out ) = run_checker($checker);
    is( $code, 1, 'workflow checker rejects newline-delimited source inventory' );
    like( $out, qr/NUL-delimited Perl inventory missing/,
        'inventory diagnostic requires filename-safe source handling' );
    write_file( $validation, $validation_source );

    my $maintenance = File::Spec->catfile(
        $tmp, '.github', 'workflows', 'agent-maintenance.yml' );
    my $maintenance_source = read_fixture_file($maintenance);
    for my $missing (
        'prove -v tests/agent_knowledge_validator/test_validator.t',
        'perl scripts/validate-agent-knowledge.pl --scope range' ) {
        my $candidate = $maintenance_source;
        $candidate =~ s/\Q$missing\E/echo missing-contract/g;
        write_file( $maintenance, $candidate );
        ( $code, $out ) = run_checker($checker);
        is( $code, 1, 'workflow checker rejects missing Perl knowledge coverage' );
        like( $out, qr/Perl knowledge contract missing/,
            'missing Perl gate diagnostic identifies the contract' );
    }
    write_file( $maintenance,
        $maintenance_source . "      - run: python3 obsolete-checker.py\n" );
    ( $code, $out ) = run_checker($checker);
    is( $code, 1, 'maintenance gate cannot reintroduce an alternate runtime' );
    like( $out, qr/interpreter policy requires Perl/,
        'runtime rejection identifies the interpreter policy' );
    return;
}

sub read_fixture_file {
    my ($path) = @_;
    open my $fh, '<', $path or BAIL_OUT("read workflow fixture: $!");
    my $text = do { local $/; <$fh> };
    close $fh or BAIL_OUT("close workflow fixture: $!");
    return $text;
}
workflow_crash_fixtures($root);

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

# --- Doxygen partial parameter documentation -------------------------------
# The source-docs lane fails on a documented function that names some
# parameters with @param and leaves others out; WARN_NO_PARAMDOC is off, so
# a block with no @param at all is intentionally accepted.
{
    my $tmp = tempdir( CLEANUP => 1 );
    my $header =
      File::Spec->catfile( $tmp, 'libi2pd', 'probe.h' );
    write_file(
        $header,
        "class Probe {\n"
          . "public:\n"
          . "    /// Encode.\n"
          . "    /// \@param highY selects.\n"
          . "    bool Encode (const uint8_t * key, uint8_t * encoded, bool highY = false) const;\n"
          . "    /// No parameter documentation at all.\n"
          . "    bool Ignored (int a, int b) const;\n"
          . "};\n"
    );
    my ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1, 'Doxygen checker rejects partial parameter docs' );
    like( $out, qr/undocumented parameter\(s\): key, encoded/,
        'missing-parameter diagnostic names the parameters' );

    write_file(
        $header,
        "class Probe {\n"
          . "public:\n"
          . "    /// Encode.\n"
          . "    /// \@param key input key.\n"
          . "    /// \@param encoded output bytes.\n"
          . "    /// \@param highY selects.\n"
          . "    bool Encode (const uint8_t * key, uint8_t * encoded, bool highY = false) const;\n"
          . "    /// No parameter documentation at all.\n"
          . "    bool Ignored (int a, int b) const;\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 0, 'Doxygen checker accepts complete and undocumented blocks' );

    # A documented name that the declaration does not have is the inverse
    # defect: Doxygen reports "argument ... is not found in the argument
    # list", and array parameters must still yield their identifier.
    write_file(
        $header,
        "class Probe {\n"
          . "public:\n"
          . "    /// Encode.\n"
          . "    /// \@param key wrong name.\n"
          . "    /// \@param encoded output bytes.\n"
          . "    bool Encode (uint8_t * encoded);\n"
          . "    /// Load.\n"
          . "    /// \@param argc count.\n"
          . "    /// \@param argv vector.\n"
          . "    bool Load (int argc, char * argv[]);\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1, 'Doxygen checker rejects a documented name not in the declaration' );
    like( $out, qr/not in the declaration: key/,
        'undeclared-parameter diagnostic names the parameter' );

    write_file(
        $header,
        "class Probe {\n"
          . "public:\n"
          . "    /// Load.\n"
          . "    /// \@param argc count.\n"
          . "    /// \@param argv vector.\n"
          . "    bool Load (int argc, char * argv[]);\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 0, 'Doxygen checker accepts an array parameter identifier' );
}

# --- Doxygen duplicated parameter documentation ----------------------------
# Doxygen merges a member's declaration and definition documentation, so a
# parameter documented on both sides is reported twice and aborts the
# source-docs lane, and the same name twice in one block is a defect too.
{
    my $tmp    = tempdir( CLEANUP => 1 );
    my $header = File::Spec->catfile( $tmp, 'libi2pd', 'probe.h' );
    my $source = File::Spec->catfile( $tmp, 'libi2pd', 'probe.cpp' );

    write_file(
        $header,
        "namespace i2p\n{\nclass Probe\n{\npublic:\n"
          . "    /// Load.\n"
          . "    /// \@param priv input scalar.\n"
          . "    /// \@param force re-derive.\n"
          . "    bool Load (const uint8_t * priv, bool force) const;\n"
          . "};\n}\n"
    );
    write_file(
        $source,
        "namespace i2p\n{\n"
          . "/**\n"
          . " * \@brief Load the scalar.\n"
          . " * \@param priv input scalar.\n"
          . " * \@param force re-derive.\n"
          . " */\n"
          . "bool Probe::Load (const uint8_t * priv, bool force) const\n"
          . "{\n"
          . "    return priv && force;\n"
          . "}\n}\n"
    );

    my ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1,
        'Doxygen checker rejects a definition repeating header @param docs' );
    like(
        $out,
        qr/duplicate \@param documentation for Load/,
        'declaration/definition duplicate names the function'
    );
    like(
        $out,
        qr/probe\.h:\d+ already documents it/,
        'duplicate diagnostic points at the declaration'
    );

    # The definition owns the parameter contract when the declaration has
    # none: this is the supported split and must stay accepted.
    write_file(
        $header,
        "namespace i2p\n{\nclass Probe\n{\npublic:\n"
          . "    bool Load (const uint8_t * priv, bool force) const;\n"
          . "};\n}\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 0,
        'Doxygen checker accepts a definition that owns the @param docs' );

    # A same-named local helper with another parameter count is a different
    # entity, so its @param block is not a duplicate of the declaration.
    write_file(
        $header,
        "namespace i2p\n{\n"
          . "/// Free helper.\n"
          . "/// \@param priv input scalar.\n"
          . "/// \@param force re-derive.\n"
          . "bool Load (const uint8_t * priv, bool force);\n}\n"
    );
    write_file(
        $source,
        "namespace i2p\n{\n"
          . "/**\n"
          . " * \@brief Local helper with an extra context.\n"
          . " * \@param ctx provider context.\n"
          . " * \@param priv input scalar.\n"
          . " * \@param force re-derive.\n"
          . " */\n"
          . "static bool Load (int ctx, const uint8_t * priv, bool force)\n"
          . "{\n"
          . "    return ctx != 0 && priv && force;\n"
          . "}\n}\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 0,
        'Doxygen checker accepts a same-named helper with another arity' );

    # The same parameter documented twice inside one block is fatal even
    # without a second declaration to merge with.
    write_file(
        $header,
        "class Probe\n{\npublic:\n"
          . "    /// Encode.\n"
          . "    /// \@param key input key.\n"
          . "    /// \@param key repeated entry.\n"
          . "    bool Encode (const uint8_t * key) const;\n"
          . "};\n"
    );
    write_file( $source, "// no definition\n" );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1,
        'Doxygen checker rejects the same @param name twice in one block' );
    like(
        $out,
        qr/duplicate \@param 'key' in one documentation block/,
        'in-block duplicate names the parameter'
    );
}

# --- Doxygen unresolved cross-references -----------------------------------
# Doxygen reports an unknown \ref target as a doc error and WARN_AS_ERROR
# makes it fatal. A type, namespace, macro or callable resolves; a generic
# data-member name such as `mutex` does not and must stay in backticks.
{
    my $tmp    = tempdir( CLEANUP => 1 );
    my $header = File::Spec->catfile( $tmp, 'libi2pd', 'probe.h' );

    write_file(
        $header,
        "enum class StorageArtifact { A };\n"
          . "class Probe\n{\npublic:\n"
          . "    /// Writes under \@ref mutex.\n"
          . "    bool Operate (int value);\n"
          . "};\n"
    );
    my ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1, 'Doxygen checker rejects an unresolved \@ref target' );
    like(
        $out,
        qr/unresolved \@ref target 'mutex'/,
        'unresolved-reference diagnostic names the target'
    );

    write_file(
        $header,
        "enum class StorageArtifact { A };\n"
          . "class Probe\n{\npublic:\n"
          . "    /// Uses \@ref StorageArtifact and \@ref Operate; the member"
          . " \`mutex\` stays in backticks.\n"
          . "    bool Operate (int value);\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 0,
        'Doxygen checker accepts type and callable references' );
}

# --- Doxygen command balance and Javadoc inline tags -----------------------
# `{\@code x}` is Javadoc syntax; Doxygen reads the `@code` as an opening
# block and aborts at the end of the comment. A real @code/@endcode pair is
# accepted, and an unclosed @verbatim is rejected.
{
    my $tmp    = tempdir( CLEANUP => 1 );
    my $header = File::Spec->catfile( $tmp, 'libi2pd', 'probe.h' );

    write_file(
        $header,
        "class Probe\n{\npublic:\n"
          . "    /// Returns {\@code ok}.\n"
          . "    bool Run ();\n"
          . "};\n"
    );
    my ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1, 'Doxygen checker rejects a Javadoc inline tag' );
    like( $out, qr/Javadoc inline tag in a C\/C\+\+ comment/,
        'inline-tag diagnostic names the correct replacement' );

    write_file(
        $header,
        "class Probe\n{\npublic:\n"
          . "    /// Returns `ok`.\n"
          . "    /// \@code\n"
          . "    /// value = 1;\n"
          . "    /// \@endcode\n"
          . "    bool Run ();\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 0, 'Doxygen checker accepts a balanced code block' );

    write_file(
        $header,
        "class Probe\n{\npublic:\n"
          . "    /// \@verbatim\n"
          . "    /// text\n"
          . "    bool Run ();\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $doxygen_checker, '--root', $tmp );
    is( $code, 1, 'Doxygen checker rejects an unclosed verbatim block' );
    like( $out, qr/unbalanced \@verbatim/,
        'unbalanced-verbatim diagnostic names the block' );
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

# --- i18n catalog escape decoding -----------------------------------------
# One left-to-right pass keeps a literal backslash-n key, a key with a
# real newline and a key with a backslash followed by a newline as three
# distinct entries. The previous ordered substitutions decoded the first
# form into a backslash-newline and merged it with the third, so this
# fixture fails on that implementation and still rejects a duplicate.
{
    my $tmp       = tempdir( CLEANUP => 1 );
    my $backslash = '\\';
    my $escaped_key =
      '"probe' . ( $backslash x 2 ) . 'nkey"';
    my $newline_key = '"probe' . $backslash . 'nkey"';
    my $backslash_newline_key =
      '"probe' . ( $backslash x 3 ) . 'nkey"';
    my $entries =
        "    { $escaped_key, \"translated\" },\n"
      . "    { $newline_key, \"translated\" },\n"
      . "    { $backslash_newline_key, \"translated\" },\n";

    for my $name (qw(Spanish.cpp Russian.cpp ChineseSimplified.cpp)) {
        write_file(
            File::Spec->catfile( $tmp, 'i18n', $name ),
            "static const LocaleStrings strings {\n" . $entries . "};\n"
        );
    }

    # The checker also harvests translatable call sites; an empty
    # source is enough and contributes no keys.
    for my $source ( 'tui/main.cpp', 'libi2pd_client/HTTPProxy.cpp',
        'daemon/Daemon.cpp', 'tools/autoconf_i2pd.cpp' )
    {
        write_file( File::Spec->catfile( $tmp, split m{/}, $source ),
            "// no translatable literal\n" );
    }

    my ( $code, $out ) = run_checker( $i18n_checker, '--root', $tmp );
    is( $code, 0, 'i18n checker keeps escaped and newline keys distinct' );
    like( $out, qr/3 keys/, 'i18n checker counts all three distinct keys' );

    write_file(
        File::Spec->catfile( $tmp, 'i18n', 'Spanish.cpp' ),
        "static const LocaleStrings strings {\n"
          . $entries
          . "    { $escaped_key, \"translated\" },\n"
          . "};\n"
    );
    ( $code, $out ) = run_checker( $i18n_checker, '--root', $tmp );
    is( $code, 1, 'i18n checker still rejects a duplicate key' );
    like( $out, qr/duplicate key/, 'duplicate-key diagnostic is emitted' );
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

# --- check-vectors unexpected pass exit status ----------------------------
SKIP: {
    skip 'the temporary executable harness uses POSIX shebang execution', 3
      if $^O eq 'MSWin32';
    my $tmp     = tempdir( CLEANUP => 1 );
    my $vectors = File::Spec->catdir( $tmp, 'vectors' );
    my $input   = "SAM fixture\n";
    my $case_id = 'sam-unexpected-pass';
    my $case = File::Spec->catfile( $vectors, "$case_id.case.json" );
    my $payload = File::Spec->catfile( $vectors, 'input.txt' );
    my $harness = File::Spec->catfile( $tmp, 'vector-harness.pl' );
    my $vector_checker =
      File::Spec->catfile( $root, 'tests', 'interop', 'check-vectors.pl' );

    write_file( $payload, $input );
    write_file(
        $case,
        JSON::PP->new->canonical->encode(
            {
                id          => $case_id,
                kind        => 'sam',
                input       => 'input.txt',
                format      => 'raw',
                sha256      => sha256_hex($input),
                expectation => 'accept',
                expect      => { status => 'accepted' },
                provenance  => {
                    java => [ { path => 'Fixture.java', line => 1,
                        contains => 'fixture' } ],
                    spec => ['fixture']
                },
                known_gap => 'intentional regression fixture'
            }
        ) . "\n"
    );
    copy(File::Spec->catfile($root, 'tools', 'tests', 'fixtures', 'vector-status-stub.pl'),
        $harness) or BAIL_OUT("copy vector status fixture: $!");
    chmod 0700, $harness or die "chmod $harness: $!\n";

    my ( $code, $out, $kind ) = run_checker(
        $vector_checker, '--vectors', $vectors, '--binary', $harness
    );
    is( $code, 1, 'vector checker fails when a known gap unexpectedly passes' );
    is( $kind, 'exit', 'unexpected pass is a normal checker failure' );
    like( $out, qr/UNEXPECTED-PASS \Q$case_id\E/,
        'unexpected pass diagnostic names the fixture' );
}

# --- child wait status decoding -------------------------------------------
{
    my ( $code, $kind, $detail ) =
      decode_wait_status( 17, -1, 0, ECHILD );
    is( $code, 255, 'waitpid failure maps to a non-zero status' );
    is( $kind, 'waitpid-error', 'waitpid failure has its own status kind' );
    is( $detail, ECHILD, 'waitpid failure retains the errno value' );

    SKIP: {
        skip 'Perl signal termination status is not portable to Win32', 3
          if $^O eq 'MSWin32';
        my $tmp = tempdir( CLEANUP => 1 );
        my $signal_child = File::Spec->catfile( $tmp, 'signal-child.pl' );
        copy(File::Spec->catfile($root, 'tools', 'tests', 'fixtures', 'signal-child.pl'),
            $signal_child) or BAIL_OUT("copy signal child fixture: $!");
        chmod 0700, $signal_child or BAIL_OUT("chmod signal child fixture: $!");
        my ( $signal_code, $out, $signal_kind, $signal ) =
          run_checker($signal_child);
        isnt( $signal_code, 0,
            'signal-terminated child is not reported as successful' );
        is( $signal_kind, 'signal',
            'run_checker distinguishes signal termination' );
        cmp_ok( $signal, '>', 0, 'signal number is retained for diagnostics' );
    }
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

=item *

F<tools/scripts/check-doxygen-params.pl> must reject a documented function
that leaves a parameter out, a parameter documented twice in one block, and a
C<.cpp> definition that repeats the C<@param> block its header declaration
already carries; it must accept a block with no C<@param>, a definition that
owns the parameter contract, and a same-named local helper with a different
parameter count.

=item *

F<i18n/check-i18n-catalogs.pl> must decode C++ escapes in one pass: a literal
backslash-n key, a real-newline key and a backslash-newline key stay distinct
entries (the previous ordered substitutions merged two of them), and a genuine
duplicate key is still rejected.

=item *

F<tests/interop/check-vectors.pl> must return failure when a differential
case marked C<known_gap> unexpectedly matches its expected value. A temporary
SAM fixture and harness isolate this exit-status contract from the checked-in
vector corpus.

=item *

C<run_checker> must distinguish normal exit, signal termination and
C<waitpid> failure. The suite uses a signal-terminated Perl child and a
synthetic C<ECHILD> decoder input to pin both non-normal paths.

=back

=head1 OUTPUT AND EXIT STATUS

TAP on STDOUT; the process exits non-zero when an assertion fails. Each
checker runs as a child Perl process, and its status and combined
STDOUT/STDERR are captured by C<run_checker>. Normal exits retain their exit
code; signal termination is reported as C<128 + signal>, and wait failures
return 255 with a distinct status kind and errno detail. Interrupted waits
retry on C<EINTR>.

The child's STDERR is merged into the captured STDOUT stream, because the
diagnostics under test are written to STDERR while the summaries go to
STDOUT, and Win32 C<select()> only accepts sockets: draining two pipes with
C<IO::Select> silently returned an empty capture on Windows and reported
every diagnostic assertion as missing.

=head1 DEPENDENCIES

Core Perl with Test::More, File::Temp, File::Path, File::Spec, File::Basename,
File::Copy, File::Find, FindBin and IPC::Open3. No compiler, build or network
access is used; the only
executed programs are the checkers under test.

=head1 PORTABILITY

Uses C<fork>-based C<open3> and POSIX wait semantics through IPC::Open3 and
C<waitpid>, which the OpenBSD base interpreter provides. The capture is a
single stream so no readiness wait on a pipe is needed; a checker that never
exits is bounded by the CTest timeout of this suite. The fixture trees are
removed through C<CLEANUP> even when an assertion fails.
