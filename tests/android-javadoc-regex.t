#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Android Javadoc extended-pattern regressions.
#
# Exercise parser-sensitive whitespace and punctuation through the real
# file-based checker. Fixtures are Java text, never executed source.

use strict;
use warnings;
use 5.016;
use Carp qw(croak);
use Errno qw(EINTR);
use File::Path qw(make_path);
use File::Spec;
use File::Temp qw(tempdir);
use FindBin qw($Bin);
use IPC::Open3 qw(open3);
use Test::More;

my $root = File::Spec->rel2abs(
    File::Spec->catdir($Bin, File::Spec->updir));
my $checker = File::Spec->catfile(
    $root, 'android', 'tools', 'check-android-javadoc.pl');
my $temporary = tempdir(CLEANUP => 1);
my $android = File::Spec->catdir($temporary, 'android');
my $java_root = File::Spec->catdir($android, qw(app src main java));
make_path($java_root);
my $java = File::Spec->catfile($java_root, 'Probe.java');

sub check_source {
    my ($text) = @_;
    open my $file, '>:raw', $java or croak "open fixture: $!";
    print {$file} $text or croak "write fixture: $!";
    close $file or croak "close fixture: $!";

    # A merged pipe keeps this harness portable to native Win32 Perl.
    # Fixed fixture output is small; the owning CTest timeout bounds a
    # checker that fails to terminate.
    my $pid = open3(my $input, my $output, undef,
        $^X, $checker, '--root', $android);
    close $input or croak "close checker input: $!";
    binmode $output, ':raw' or croak "set checker output mode: $!";
    local $/ = undef;
    my $captured = <$output> // '';
    close $output or croak "close checker output: $!";
    my $waited;
    do { $waited = waitpid($pid, 0); }
      while $waited < 0 && $! == EINTR;
    croak 'cannot reap checker' unless $waited == $pid;
    my $status = $?;
    return (255, $captured) if $status < 0 || ($status & 127);
    return ($status >> 8, $captured);
}

my $source = <<'JAVA';
package org.openi2pd.router;
class Probe {
    /**
     * Keep {@code command <tunnel> # literal spaces} as code.
     * Keep {@literal <opaque> # literal spaces} as text.
     * Refer to {@link Probe#consume} without scanning the member hash.
     * <p>Ordinary supported HTML remains accepted.</p>
     * @param values values with a generic comma
     * @param names trailing varargs
     */
    static void consume(@Nullable final Map<String, Integer> values,
            String... names) {
    }
}
JAVA

my ($code, $out) = check_source($source);
is($code, 0, 'inline punctuation and generic/vararg operands are accepted');
like($out, qr/OK:\ no\ Javadoc\ defects/x, 'accepted source has clean summary');

my $crlf = $source;
$crlf =~ s/\n/\r\n/gx;
($code, $out) = check_source($crlf);
is($code, 0, 'CRLF and indentation retain declaration operand boundaries');

my $missing = $source;
$missing =~ s/\@param\ names/\@param missing/x;
($code, $out) = check_source($missing);
is($code, 1, 'a missing parameter remains an error after whitespace folding');
like($out, qr/\@param\ missing\ is\ not\ a\ parameter/x,
    'parameter diagnostic retains the exact operand');
like($out, qr/Map<String,\ Integer>\ values,\ String\.\.\.\ names/x,
    'folded declaration retains one literal separator space');

my $placeholder = $source;
$placeholder =~ s/Ordinary\ supported\ HTML/<tunnel>/x;
($code, $out) = check_source($placeholder);
is($code, 1, 'unescaped HTML-like placeholders remain errors');
like($out, qr/<tunnel>\ is\ parsed\ as\ HTML/x,
    'raw placeholder diagnostic survives inline-tag stripping');

done_testing();
