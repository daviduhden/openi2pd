#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
# CLI regressions; invoked only by manual CI, never by source inspection.
use 5.016;
use strict;
use warnings;
use Test::More;
use FindBin qw($Bin);
use File::Spec;
use File::Temp qw(tempdir tempfile);
use IPC::Open3 qw(open3);
use IO::Socket::INET;
use POSIX qw(WIFEXITED WEXITSTATUS);
use Errno qw(EINTR);

my $root = File::Spec->rel2abs(File::Spec->catdir($Bin, '..', '..'));
my $temporary = tempdir(CLEANUP => 1);
sub invoke {
    my ($name, @arguments) = @_;
    my ($output, $output_path) = tempfile(DIR => $temporary);
    my ($errors, $error_path) = tempfile(DIR => $temporary);
    binmode $output;
    binmode $errors;
    my $pid = open3(my $input, '>&' . fileno($output),
        '>&' . fileno($errors), $^X,
        File::Spec->catfile($root, 'tests', 'interop', $name), @arguments);
    close $input;
    my $waited;
    do { $waited = waitpid($pid, 0) } while $waited < 0 && $! == EINTR;
    $waited == $pid or die "wait for helper failed\n";
    my $status = $?;
    seek $output, 0, 0 or die "rewind output failed\n";
    seek $errors, 0, 0 or die "rewind errors failed\n";
    local $/;
    my $out = <$output> // '';
    my $err = <$errors> // '';
    close $output;
    close $errors;
    return (WIFEXITED($status) ? WEXITSTATUS($status) : 255, $out, $err);
}
sub fixture {
    my ($name, $data) = @_;
    my $path = File::Spec->catfile($temporary, $name);
    open my $file, '>:raw', $path or die "create fixture failed\n";
    print {$file} $data or die "write fixture failed\n";
    close $file or die "close fixture failed\n";
    return $path;
}
my $valid = fixture('quoted space.hex', "# comment\n00 41 ff\n");
my ($code, $out, $err) = invoke('hex-to-binary.pl', $valid);
is($code, 0, 'valid commented hexadecimal succeeds');
is($out, pack('H*', '0041ff'), 'stdout is exact binary including NUL');
is($err, '', 'valid conversion has no diagnostics');
for my $bad ('0', 'zz', '00#inline') {
    ($code, $out, $err) = invoke('hex-to-binary.pl', fixture('bad.hex', $bad));
    isnt($code, 0, 'malformed hexadecimal fails');
    is($out, '', 'failure publishes no partial binary');
}
($code, $out, $err) = invoke('hex-to-binary.pl');
isnt($code, 0, 'missing conversion operand fails');
my $secret = fixture('request.txt', "SAM SECRET_SENTINEL\n");
for my $host ('example.invalid', '127.0.0.2', '::1') {
    ($code, $out, $err) = invoke('sam-stream-send.pl', $host, 17656, $secret);
    isnt($code, 0, 'nonapproved endpoint fails before networking');
    unlike($out . $err, qr/SECRET_SENTINEL/, 'request remains private');
}
($code, $out, $err) = invoke('sam-stream-send.pl', '127.0.0.1', 17656,
    fixture('oversized-request.txt', 'x' x 65537));
isnt($code, 0, 'oversized request fails before connecting');
like($err, qr/SAM fixture exceeds limit/, 'request bound is explicit');
is($out, '', 'oversized request has no published content');
($code, $out, $err) = invoke('sam-stream-send.pl', '127.0.0.1', 17656, $secret);
isnt($code, 0, 'non-HELLO request fails before networking');
unlike($out . $err, qr/SECRET_SENTINEL/, 'invalid request diagnostics are private');
my $hello = fixture('hello.txt', "HELLO VERSION MIN=3.0 MAX=3.3\n");
for my $response (
    ["HELLO REPLY RESULT=OK VERSION=3.3\n", 0],
    ["HELLO REPLY RESULT=NOVERSION\n", 0],
    ["HELLO REPLY RESULT=I2P_ERROR\n", 0],
    ["HELLO REPLY RESULT=OK MESSAGE=REPLY_SECRET\n", 255]
) {
SKIP: {
    skip 'fork-based local server requires POSIX', 4 if $^O eq 'MSWin32';
    my $listener = IO::Socket::INET->new(LocalAddr => '127.0.0.1',
        LocalPort => 0, Listen => 1, Proto => 'tcp')
        or die "create fixture listener failed\n";
    my $port = $listener->sockport;
    my $pid = fork();
    defined $pid or die "fork fixture failed\n";
    if (!$pid) {
        local $SIG{ALRM} = sub { POSIX::_exit(91) };
        alarm 15;
        my $peer = $listener->accept or POSIX::_exit(92);
        my $line = <$peer>;
        defined $line && $line eq "HELLO VERSION MIN=3.0 MAX=3.3\n"
          or POSIX::_exit(93);
        print {$peer} $response->[0] or POSIX::_exit(94);
        close $peer;
        POSIX::_exit(0);
    }
    close $listener;
    ($code, $out, $err) = invoke('sam-stream-send.pl', '127.0.0.1', $port, $hello);
    if ($response->[1] == 255) {
        isnt($code, 0, 'unexpected reply fields fail');
        is($out, '', 'unexpected reply is not published');
    } else {
        is($code, $response->[1], 'valid exchange succeeds regardless of negotiation result');
        is($out, "$hello -> $response->[0]", 'negotiated result remains observable');
    }
    unlike($out . $err, qr/SECRET_SENTINEL|REPLY_SECRET/, 'no protocol data logged');
    waitpid($pid, 0) == $pid or die "wait for server failed\n";
    is($?, 0, 'server observes exact request');
}
}
done_testing();
