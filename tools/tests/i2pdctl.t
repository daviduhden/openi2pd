#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: i2pdctl.
#
# Regression tests for tools/scripts/i2pdctl against a self-contained loopback
# I2PControl server. The HTTPS fallback is exercised with a temporary openssl
# test double, so no internet or real certificate is needed.
#

use strict;
use warnings;

use FindBin qw($Bin);
use File::Spec;
use File::Temp qw(tempdir);
use IO::Socket::INET;
use IO::Select;
use IPC::Open3 ();
use JSON::PP ();
use Symbol qw(gensym);
use Test::More;

# The harness uses fork() and IO::Select around a loopback listener. Windows
# Perl does not provide a portable fork emulation for this pattern, so keep the
# cross-platform unit coverage and run the POSIX integration harness here.
plan skip_all => 'i2pdctl integration harness requires POSIX fork/select' if $^O eq 'MSWin32';

my $script = File::Spec->catfile($Bin, File::Spec->updir, 'scripts', 'i2pdctl');

my ($server_pid, $server_port);

END {
    if ($server_pid) {
        kill 'TERM', $server_pid;
        waitpid($server_pid, 0);
    }
}

# ---------------------------------------------------------------------------
# Fake I2PControl server
# ---------------------------------------------------------------------------

my %ROUTER_RESULT = (
    'i2p.router.uptime'                    => '5000',
    'i2p.router.net.status'                => 0,
    'i2p.router.netdb.knownpeers'          => 12,
    'i2p.router.netdb.activepeers'         => 3,
    'i2p.router.net.bw.inbound.1s'         => 1024,
    'i2p.router.net.bw.outbound.1s'        => 512,
    'i2p.router.net.tunnels.participating' => 5,
    'i2p.router.net.tunnels.successrate'   => 87,
    'i2p.router.net.total.received.bytes'  => 2048,
    'i2p.router.net.total.sent.bytes'      => 1024,
);

my %SERVICES_RESULT = (
    'HTTPProxy' => { enabled => 'true' },
    'SOCKS'     => { enabled => 'false' },
    'BOB'       => { enabled => 'false' },
    'SAM'       => { enabled => 'false' },
    'I2CP'      => { enabled => 'false' },
    'I2PTunnel' => {
        client => {
            example       => { address => 'abc.b32.i2p' },
            "bad\x1bname" => { address => 'x' },
            c1            => { address => "\x{9b}evil" },
        },
        server => { web     => { address => 'def.b32.i2p', port => 8080 } },
    },
);

sub _read_http_request {
    my ($socket) = @_;

    my $buffer = '';
    my $header_end;
    while (1) {
        my $read = sysread($socket, my $chunk, 4096);
        return undef unless defined $read && $read > 0;
        $buffer .= $chunk;
        $header_end = index($buffer, "\r\n\r\n");
        last if $header_end >= 0;
        return undef if length($buffer) > 65_536;
    }

    my $head = substr($buffer, 0, $header_end);
    my $body = substr($buffer, $header_end + 4);
    my ($length) = $head =~ /^Content-Length:\s*(\d+)\s*$/mi;
    $length = 0 unless defined $length;

    while (length($body) < $length) {
        my $wanted = $length - length($body);
        my $read = sysread($socket, my $chunk, $wanted);
        return undef unless defined $read && $read > 0;
        $body .= $chunk;
    }

    return ($head, $body);
}

sub _write_all {
    my ($socket, $data) = @_;
    my $offset = 0;
    while ($offset < length($data)) {
        my $written = syswrite($socket, $data, length($data) - $offset, $offset);
        return unless defined $written && $written > 0;
        $offset += $written;
    }
    return 1;
}

sub _rpc_error {
    my ($code, $message) = @_;
    return {
        id      => undef,
        error   => { code => $code, message => $message },
        jsonrpc => '2.0',
    };
}

sub _serve_connection {
    my ($socket) = @_;

    my ($head, $body) = _read_http_request($socket);
    return unless defined $head;

    my ($request_line) = split /\r?\n/, $head;
    return unless defined $request_line
        && $request_line =~ m{\APOST\s+\S+\s+HTTP/1\.[01]\z};

    my $request = eval { JSON::PP->new->utf8->decode($body) };
    my $response;

    if (!$request || ref $request ne 'HASH') {
        $response = _rpc_error(-32700, 'Parse error');
    }
    elsif (($request->{method} // '') eq 'Authenticate') {
        my $password = $request->{params}{Password} // '';
        $response = ($password eq 'itoopie' || $password eq "p\x{e4}ss")
            ? {
                id      => 1,
                result  => { API => 1, Token => 'test-token' },
                jsonrpc => '2.0',
            }
            : { id => 1, result => {}, jsonrpc => '2.0' };
    }
    else {
        my $token = $request->{params}{Token} // '';
        if ($token ne 'test-token') {
            $response = _rpc_error(-32602, 'Invalid or missing token');
        }
        elsif (($request->{method} // '') eq 'RouterInfo') {
            $response = {
                id      => 1,
                result  => \%ROUTER_RESULT,
                jsonrpc => '2.0',
            };
        }
        elsif (($request->{method} // '') eq 'ClientServicesInfo') {
            $response = {
                id      => 1,
                result  => \%SERVICES_RESULT,
                jsonrpc => '2.0',
            };
        }
        elsif (($request->{method} // '') eq 'Echo') {
            $response = {
                id      => 1,
                result  => { Result => $request->{params}{Echo} // '' },
                jsonrpc => '2.0',
            };
        }
        elsif (($request->{method} // '') eq 'RouterManager') {
            $response = { id => 1, result => {}, jsonrpc => '2.0' };
        }
        else {
            $response = _rpc_error(-32601, 'Method not found');
        }
    }

    my $json = JSON::PP->new->utf8->canonical->encode($response);
    my $http = "HTTP/1.1 200 OK\r\n"
        . "Content-Type: application/json\r\n"
        . 'Content-Length: ' . length($json) . "\r\n"
        . "Connection: close\r\n"
        . "\r\n"
        . $json;

    _write_all($socket, $http);
    return;
}

sub start_fake_server {
    my $listener = IO::Socket::INET->new(
        LocalAddr => '127.0.0.1',
        LocalPort => 0,
        Proto     => 'tcp',
        Listen    => 5,
        ReuseAddr => 1,
    ) or BAIL_OUT("cannot create fake server socket: $!");

    my $port = $listener->sockport;
    my $pid = fork();
    BAIL_OUT("cannot fork fake server: $!") unless defined $pid;

    if ($pid == 0) {
        $SIG{TERM} = sub { exit 0 };
        $SIG{PIPE} = 'IGNORE';
        while (my $connection = $listener->accept) {
            _serve_connection($connection);
            close $connection;
        }
        exit 0;
    }

    close $listener;
    $server_pid = $pid;
    $server_port = $port;
    return $port;
}

# ---------------------------------------------------------------------------
# Command helpers
# ---------------------------------------------------------------------------

sub _run_command {
    my ($environment, $command, @arguments) = @_;

    my ($stdin, $stdout, $stderr) = (gensym(), gensym(), gensym());
    my $pid;
    {
        local %ENV = %ENV;
        for my $name (keys %{$environment}) {
            if (defined $environment->{$name}) {
                $ENV{$name} = $environment->{$name};
            }
            else {
                delete $ENV{$name};
            }
        }
        $pid = IPC::Open3::open3($stdin, $stdout, $stderr, $command, @arguments);
    }
    close $stdin;

    my $selector = IO::Select->new($stdout, $stderr);
    my $stdout_fd = fileno($stdout);
    my ($out, $err) = ('', '');
    my $deadline = time + 30;

    while ($selector->count) {
        my @ready = $selector->can_read(1);
        if (!@ready) {
            if (time > $deadline) {
                kill 'KILL', $pid;
                last;
            }
            next;
        }

        for my $handle (@ready) {
            my $read = sysread($handle, my $chunk, 65_536);
            if (!defined $read) {
                $selector->remove($handle);
                next;
            }
            if ($read == 0) {
                $selector->remove($handle);
                close $handle;
                next;
            }
            my $fd = fileno($handle);
            if (defined $fd && defined $stdout_fd && $fd == $stdout_fd) {
                $out .= $chunk;
            }
            else {
                $err .= $chunk;
            }
        }
    }

    waitpid($pid, 0);
    my $status = $?;
    my $code = $status == -1
        ? 255
        : ($status & 127 ? 128 + ($status & 127) : $status >> 8);

    return ($out, $err, $code);
}

sub run_i2pdctl {
    my (@arguments) = @_;
    return _run_command(
        {
            I2PCONTROL_URL      => "http://127.0.0.1:$server_port/",
            I2PCONTROL_PASSWORD => 'itoopie',
            I2PDCTL_TLS_BACKEND => 'auto',
        },
        $^X,
        $script,
        @arguments,
    );
}

sub decode_json {
    my ($text) = @_;
    my $value = eval { JSON::PP->new->utf8->decode($text) };
    return $value;
}

# A portable `openssl s_client` test double.  It records its argv, drains the
# request body (which also proves that the client closes the child's stdin) and
# emits a canned response selected by I2PDCTL_TEST_TLS_MODE.  Only shell
# builtins are used so the shim works with PATH pointing at its own directory.
sub write_openssl_shim {
    my ($directory) = @_;
    my $path = File::Spec->catfile($directory, 'openssl');
    open my $handle, '>', $path
        or BAIL_OUT("cannot create openssl shim: $!");
    print {$handle} <<'SHIM';
#!/bin/sh
if [ -n "$I2PDCTL_TEST_ARGV_LOG" ]; then
    : > "$I2PDCTL_TEST_ARGV_LOG" || exit 90
    for arg in "$@"; do
        printf '%s\n' "$arg" >> "$I2PDCTL_TEST_ARGV_LOG" || exit 91
    done
fi
while IFS= read -r _line; do :; done
case "$I2PDCTL_TEST_TLS_MODE" in
    content_length)
        body='{"id":1,"result":{"Token":"t"},"jsonrpc":"2.0"}'
        printf 'HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s' "${#body}" "$body"
        ;;
    ok)
        body='{"id":1,"result":{"Token":"t"},"jsonrpc":"2.0"}'
        printf 'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n'
        printf '%x\r\n%s\r\n0\r\n\r\n' "${#body}" "$body"
        ;;
    truncated)
        printf 'HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{"short":1}'
        ;;
    invalid_content_length)
        printf 'HTTP/1.1 200 OK\r\nContent-Length: +5\r\n\r\n{}'
        ;;
    malformed_chunk)
        printf 'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n'
        ;;
    oversized_chunk)
        printf 'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFF\r\n'
        ;;
    oversized_content_length)
        printf 'HTTP/1.1 200 OK\r\nContent-Length: 9999999\r\n\r\n'
        ;;
    incomplete)
        printf 'HTTP/1.1 200 OK\r\nContent-Length: 2'
        ;;
    unsupported_encoding)
        printf 'HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nx'
        ;;
    bad_status)
        printf 'HTTP/1.1 nope\r\nContent-Length: 2\r\n\r\n{}'
        ;;
    http_error)
        printf 'HTTP/1.1 500 Internal Server Error\r\nContent-Length: 2\r\n\r\n{}'
        ;;
    invalid_json)
        printf 'HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx'
        ;;
    stderr)
        printf '\033[31mboom\033[0m\n' >&2
        exit 1
        ;;
    empty)
        exit 0
        ;;
    *)
        printf 'openssl shim: unknown mode\n' >&2
        exit 92
        ;;
esac
exit 0
SHIM
    close $handle or BAIL_OUT("cannot write openssl shim: $!");
    chmod 0755, $path
        or BAIL_OUT("cannot make openssl shim executable: $!");
    return $path;
}

sub run_with_openssl_shim {
    my ($environment, @arguments) = @_;
    return _run_command($environment, $^X, $script, @arguments);
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

ok(-f $script, 'i2pdctl exists');

{
    my ($out, $err, $code) = _run_command({}, $^X, '-c', $script);
    is($code, 0, 'perl -c succeeds');
    like($err, qr/syntax OK/, 'perl -c reports syntax OK');
}

start_fake_server();
ok($server_port > 0, 'fake I2PControl server is listening');

{
    my ($out, $err, $code) = run_i2pdctl('-h');
    is($code, 0, '--help exits 0');
    like($out, qr/usage: i2pdctl \[-h\] \{exec,raw_info,info\}/,
        'main usage is printed');
    like($out, qr/raw_info/, 'main help lists raw_info');
}

{
    my ($out, $err, $code) = run_i2pdctl();
    is($code, 0, 'no command prints help and exits 0');
    like($out, qr/positional arguments:/, 'no command prints help');
}

{
    my ($out, $err, $code) = run_i2pdctl('bogus');
    is($code, 2, 'unknown command exits 2');
    like($err, qr/invalid choice: 'bogus'/, 'unknown command is reported');
    like($err, qr/usage: i2pdctl/, 'usage is printed on argument error');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec');
    is($code, 2, 'exec without arguments exits 2');
    like($err, qr/required: method, parameters/, 'missing exec arguments reported');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec', 'Echo');
    is($code, 2, 'exec without parameters exits 2');
    like($err, qr/required: parameters/, 'missing parameters reported');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec', 'Echo', 'not-json');
    is($code, 0, 'invalid parameters preserve the old zero exit status');
    like($out, qr/\AError: parameters should be a valid JSON object\n\z/,
        'invalid JSON parameters message is preserved');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec', 'Echo', '[]');
    is($code, 0, 'non-object parameters preserve the old zero exit status');
    like($out, qr/parameters should be a valid JSON object/,
        'non-object parameters are rejected as an object');
}

{
    my ($out, $err, $code) = run_i2pdctl('raw_info', 'Nope');
    is($code, 0, 'unknown info method exits 0 like the old script');
    like($out, qr/Invalid method Nope\. Supported methods are RouterInfo, ClientServicesInfo/,
        'unknown info method message is preserved');
}

{
    my ($out, $err, $code) = run_i2pdctl('raw_info');
    is($code, 0, 'raw_info without methods exits 0');
    is($out, "{}\n", 'raw_info without methods prints an empty object');
}

{
    my ($out, $err, $code) = run_i2pdctl('raw_info', 'RouterInfo');
    is($code, 0, 'raw_info RouterInfo exits 0');
    my $decoded = decode_json($out);
    is(ref $decoded, 'HASH', 'raw_info output is JSON');
    is($decoded->{RouterInfo}{'i2p.router.uptime'}, '5000',
        'raw_info returns the requested result');
}

{
    my ($out, $err, $code) = run_i2pdctl('raw_info', 'RouterInfo', 'RouterInfo');
    is($code, 0, 'duplicate raw_info methods exit 0');
    my $key_count = () = $out =~ /"RouterInfo"/g;
    is($key_count, 1, 'duplicate raw_info methods produce one JSON key');
}

{
    my ($out, $err, $code) = _run_command(
        {
            I2PCONTROL_URL      => "http://127.0.0.1:$server_port/",
            I2PCONTROL_PASSWORD => 'wrong-password',
            I2PDCTL_TLS_BACKEND => 'auto',
        },
        $^X, $script, 'exec', 'Echo', '{}',
    );
    is($code, 1, 'authentication failure exits 1');
    like($err, qr/authentication failed \(no token returned\)/,
        'authentication failure is reported');
    is($out, '', 'authentication failure prints no result');
}

{
    my ($out, $err, $code) = _run_command(
        {
            I2PCONTROL_URL      => "http://127.0.0.1:$server_port/",
            I2PCONTROL_PASSWORD => "p\x{c3}\x{a4}ss",
            I2PDCTL_TLS_BACKEND => 'auto',
        },
        $^X, $script, 'exec', 'Echo', '{"Echo":"utf8"}',
    );
    is($code, 0, 'a UTF-8 password authenticates');
    like($out, qr/'Result': 'utf8'/, 'the UTF-8-password request succeeds');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec', 'Echo', '{"Echo":"hello"}', '-j');
    is($code, 0, 'exec -j exits 0');
    my $decoded = decode_json($out);
    is(ref $decoded, 'HASH', 'exec -j prints raw JSON');
    is($decoded->{result}{Result}, 'hello', 'exec -j carries the result');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec', '--json-output', 'Echo', '{"Echo":"world"}');
    is($code, 0, 'exec --json-output exits 0');
    my $decoded = decode_json($out);
    is($decoded->{result}{Result}, 'world', 'exec --json-output carries the result');
}

{
    my ($out, $err, $code) = run_i2pdctl('exec', 'Echo', '{"Echo":"hello"}');
    is($code, 0, 'exec without -j exits 0');
    like($out, qr/'Result': 'hello'/, 'exec human output contains the result');
}

{
    my ($out, $err, $code) = run_i2pdctl('info');
    is($code, 0, 'info exits 0');
    like($out, qr/### Router info/, 'info prints the router section');
    like($out, qr/Uptime: 0:00:05/, 'uptime is humanized');
    like($out, qr/Status: OK/, 'router status is mapped');
    like($out, qr/Tunnel creation success rate: 87%/, 'tunnel success rate is shown');
    like($out, qr/Received: 2\.0KB \(1024 B\/s\)  \/  Sent: 1024B \(512 B\/s\)/,
        'bandwidth is humanized with the historical B/s suffix');
    like($out, qr/Known routers: 12  \/  Active: 3/, 'peer counts are shown');
    like($out, qr/### Interfaces/, 'info prints the interfaces section');
    like($out, qr/- HTTPProxy: ON/, 'enabled interface is ON');
    like($out, qr/- SOCKS: OFF/, 'disabled interface is OFF');
    like($out, qr/### Client I2P Tunnels/, 'client tunnels are listed');
    like($out, qr/- example \{'address': 'abc\.b32\.i2p'\}/, 'client tunnel entry is shown');
    unlike($out, qr/\x1b/, 'control characters from the server are not sent to the terminal');
    like($out, qr/- bad\\x1bname \{'address': 'x'\}/,
        'control characters in tunnel names are escaped for display');
    unlike($out, qr/[\x{80}-\x{9f}]/, 'C1 control characters are not sent to the terminal');
    like($out, qr/\\x\{9b\}evil/, 'C1 control characters in values are escaped');
    like($out, qr/### Server I2P Tunnels/, 'server tunnels are listed');
    like($out, qr/- web def\.b32\.i2p:8080/, 'server tunnel port is shown');
}

{
    my ($out, $err, $code) = _run_command(
        {
            I2PCONTROL_URL      => 'http://127.0.0.1:1/',
            I2PDCTL_TLS_BACKEND => 'auto',
        },
        $^X, $script, 'info',
    );
    is($code, 1, 'unavailable URL exits 1');
    like($out, qr/I2PControl URL is unavailable\. Check your openi2pd settings and network connection\./,
        'unavailable URL message is preserved');
}

{
    my ($out, $err, $code) = _run_command(
        { I2PCONTROL_URL => 'ftp://127.0.0.1:7650/' },
        $^X, $script, 'info',
    );
    is($code, 1, 'unsupported URL scheme exits 1');
    like($err, qr/invalid I2PControl URL/, 'unsupported URL scheme is rejected');
}

{
    # A hostile environment must not redirect the management password/token
    # through an HTTP proxy.  The local fake server is reachable directly.
    my ($out, $err, $code) = _run_command(
        {
            I2PCONTROL_URL      => "http://127.0.0.1:$server_port/",
            I2PCONTROL_PASSWORD => 'itoopie',
            I2PDCTL_TLS_BACKEND => 'auto',
            http_proxy          => 'http://127.0.0.1:1/',
            HTTP_PROXY          => 'http://127.0.0.1:1/',
            https_proxy         => 'http://127.0.0.1:1/',
            HTTPS_PROXY         => 'http://127.0.0.1:1/',
            all_proxy           => 'http://127.0.0.1:1/',
            ALL_PROXY           => 'http://127.0.0.1:1/',
        },
        $^X, $script, 'exec', 'Echo', '{"Echo":"direct"}',
    );
    is($code, 0, 'proxy environment variables are ignored');
    like($out, qr/'Result': 'direct'/,
        'the direct loopback request is not sent through a proxy');
}

{
    my ($first_out, $first_err, $first_code) =
        run_i2pdctl('exec', 'Echo', '{"Echo":"stable"}');
    my ($second_out, $second_err, $second_code) =
        run_i2pdctl('exec', 'Echo', '{"Echo":"stable"}');
    is($first_code, 0, 'first repeated request exits 0');
    is($second_code, 0, 'second repeated request exits 0');
    is($second_out, $first_out, 'repeated requests are idempotent');
}

# ---------------------------------------------------------------------------
# URL validation
# ---------------------------------------------------------------------------

{
    my @rejected = (
        ['userinfo',        'http://user:pass@127.0.0.1:7650/'],
        ['port zero',       'http://127.0.0.1:0/'],
        ['port too large',  'http://127.0.0.1:99999/'],
        ['option-like host', 'http://-option:7650/'],
        ['trailing dot',    'http://127.0.0.1.:7650/'],
        ['CRLF in path',    "http://127.0.0.1:7650/\r\nX-Injected: 1"],
        ['space in path',   'http://127.0.0.1:7650/a b'],
        ['empty authority', 'http:///path'],
        ['bad brackets',    'http://[::1/'],
        ['oversized',       'http://127.0.0.1:7650/' . ('a' x 5000)],
    );

    for my $case (@rejected) {
        my ($label, $url) = @{$case};
        my ($out, $err, $code) = _run_command(
            { I2PCONTROL_URL => $url, I2PCONTROL_PASSWORD => 'x' },
            $^X, $script, 'exec', 'Echo', '{}',
        );
        is($code, 1, "malformed URL ($label) exits 1");
        like($err, qr/invalid I2PControl URL/,
            "malformed URL ($label) is rejected");
    }
}

# ---------------------------------------------------------------------------
# HTTPS transport through the openssl fallback
# ---------------------------------------------------------------------------

sub read_argv_log {
    my ($path) = @_;
    open my $handle, '<', $path or return [];
    my @lines = <$handle>;
    close $handle;
    chomp @lines;
    return \@lines;
}

SKIP: {
    skip 'the openssl shim needs a POSIX shell', 1 if $^O eq 'MSWin32';

    subtest 'openssl s_client fallback transport' => sub {
        plan tests => 40;
        my $shim_dir  = tempdir(CLEANUP => 1);
        my $empty_dir = tempdir(CLEANUP => 1);
        write_openssl_shim($shim_dir);
        my $log = File::Spec->catfile($shim_dir, 'argv.log');

        my %base = (
            PATH                  => join(':', $shim_dir, $ENV{PATH} // ''),
            I2PCONTROL_PASSWORD   => 'itoopie',
            I2PDCTL_TLS_BACKEND   => 'openssl',
            I2PDCTL_TEST_ARGV_LOG => $log,
            I2PCONTROL_URL        => 'https://127.0.0.1:7650/',
        );

        my $run = sub {
            my ($mode, $url) = @_;
            my %environment = (
                %base,
                I2PCONTROL_URL       => $url,
                I2PDCTL_TEST_TLS_MODE => $mode,
            );
            return run_with_openssl_shim(
                \%environment, 'exec', 'Echo', '{"Echo":"hello"}',
            );
        };

        my ($out, $err, $code) = $run->('ok', 'https://127.0.0.1:7650/');
        is($code, 0, 'chunked TLS response is accepted');
        is($out, "{'Token': 't'}\n", 'chunked TLS response is decoded');

        ($out, $err, $code) = $run->('content_length', 'https://127.0.0.1:7650/');
        is($code, 0, 'Content-Length TLS response is accepted');
        is($out, "{'Token': 't'}\n", 'Content-Length TLS response is decoded');

        ($out, $err, $code) = $run->('truncated', 'https://127.0.0.1:7650/');
        is($code, 1, 'truncated Content-Length exits 1');
        like($err, qr/truncated response from I2PControl/,
            'truncated Content-Length is rejected');

        ($out, $err, $code) = $run->('invalid_content_length',
            'https://127.0.0.1:7650/');
        is($code, 1, 'non-numeric Content-Length exits 1');
        like($err, qr/invalid Content-Length from I2PControl/,
            'non-numeric Content-Length is rejected');

        ($out, $err, $code) = $run->('malformed_chunk', 'https://127.0.0.1:7650/');
        is($code, 1, 'malformed chunk exits 1');
        like($err, qr/malformed chunked response from I2PControl/,
            'malformed chunk size is rejected');

        ($out, $err, $code) = $run->('oversized_chunk', 'https://127.0.0.1:7650/');
        is($code, 1, 'oversized chunk exits 1');
        like($err, qr/response from I2PControl exceeds size limit/,
            'oversized chunk is rejected before allocation');

        ($out, $err, $code) = $run->('oversized_content_length',
            'https://127.0.0.1:7650/');
        is($code, 1, 'oversized Content-Length exits 1');
        like($err, qr/response from I2PControl exceeds size limit/,
            'oversized Content-Length is rejected before allocation');

        ($out, $err, $code) = $run->('incomplete', 'https://127.0.0.1:7650/');
        is($code, 1, 'incomplete HTTP header exits 1');
        like($err, qr/invalid HTTP response from I2PControl/,
            'incomplete HTTP header is rejected');

        ($out, $err, $code) = $run->('unsupported_encoding',
            'https://127.0.0.1:7650/');
        is($code, 1, 'unsupported Transfer-Encoding exits 1');
        like($err, qr/unsupported Transfer-Encoding from I2PControl/,
            'unsupported Transfer-Encoding is rejected');

        ($out, $err, $code) = $run->('bad_status', 'https://127.0.0.1:7650/');
        is($code, 1, 'malformed status line exits 1');
        like($err, qr/invalid HTTP status from I2PControl/,
            'malformed status line is rejected');

        ($out, $err, $code) = $run->('http_error', 'https://127.0.0.1:7650/');
        is($code, 1, 'non-2xx TLS response exits 1');
        like($out, qr/I2PControl URL is unavailable/,
            'non-2xx TLS response reports an unavailable service');

        ($out, $err, $code) = $run->('invalid_json', 'https://127.0.0.1:7650/');
        is($code, 1, 'invalid JSON body exits 1');
        like($err, qr/invalid JSON response from I2PControl/,
            'invalid JSON body is rejected');

        ($out, $err, $code) = $run->('stderr', 'https://127.0.0.1:7650/');
        is($code, 1, 'openssl failure exits 1');
        like($err, qr/TLS request to I2PControl failed: boom/,
            'openssl diagnostics are reported');
        unlike($err, qr/\e/, 'openssl diagnostics are stripped of escape sequences');

        ($out, $err, $code) = $run->('empty', 'https://127.0.0.1:7650/');
        is($code, 1, 'empty TLS response exits 1');
        like($err, qr/TLS request to I2PControl failed/,
            'empty TLS response is reported as a failure');

        unlink $log;
        ($out, $err, $code) = $run->('ok', 'https://example.invalid:7650/');
        is($code, 0, 'hostname TLS target is accepted');
        is_deeply(
            read_argv_log($log),
            ['s_client', '-quiet', '-ign_eof', '-connect',
                'example.invalid:7650', '-servername', 'example.invalid'],
            'openssl receives an argument list and SNI for hostnames',
        );

        unlink $log;
        ($out, $err, $code) = $run->('ok', 'https://[::1]:7650/');
        is($code, 0, 'IPv6 TLS target is accepted');
        is_deeply(
            read_argv_log($log),
            ['s_client', '-quiet', '-ign_eof', '-connect', '[::1]:7650'],
            'IPv6 -connect target is bracketed and has no SNI',
        );

        unlink $log;
        ($out, $err, $code) = _run_command(
            {
                PATH                => $empty_dir,
                I2PCONTROL_URL      => 'https://127.0.0.1:7650/',
                I2PDCTL_TLS_BACKEND => 'openssl',
            },
            $^X, $script, 'exec', 'Echo', '{}',
        );
        is($code, 1, 'missing openssl exits 1');
        is($out, '', 'missing openssl prints nothing to stdout');
        like($err, qr/refusing to use cleartext/,
            'missing openssl refuses to downgrade to cleartext');
        ok(!(-e $log), 'missing openssl starts no transport');

        unlink $log;
        ($out, $err, $code) = _run_command(
            {
                PATH                => join(':', $shim_dir, $ENV{PATH} // ''),
                I2PCONTROL_URL      => 'https://127.0.0.1:7650/',
                I2PDCTL_TLS_BACKEND => 'cleartext',
            },
            $^X, $script, 'exec', 'Echo', '{}',
        );
        is($code, 1, 'invalid TLS backend exits 1');
        like($err, qr/invalid I2PDCTL_TLS_BACKEND/,
            'invalid TLS backend is rejected');
        ok(!(-e $log), 'invalid TLS backend starts no transport');
    };
}

done_testing();
