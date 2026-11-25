#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
# Send one bounded fixture and read one reply under a total deadline.
# Only numeric loopback addresses are accepted; no public I2P traffic.
use 5.016;
use strict;
use warnings;
use IO::Socket::INET;
local $SIG{PIPE} = 'IGNORE';

@ARGV == 3 or die "usage: sam-stream-send.pl 127.0.0.1 PORT FILE\n";
my $numeric_version = qr/ [0-9]+ (?: \. [0-9]+ )* /x;
my $line_end        = qr/ \r? \n \z /x;

sub read_request {
    my ($path) = @_;
    my $request = '';
    open my $input, '<:raw', $path or die "cannot open SAM fixture\n";
    while (1) {
        my $count = read $input, my $chunk, 8192;
        defined $count or die "cannot read SAM fixture\n";
        last unless $count;
        $request .= $chunk;
        length($request) <= 65536 or die "SAM fixture exceeds limit\n";
    }
    close $input or die "cannot close SAM fixture\n";
    return $request;
}

# This diagnostic client accepts only credential-free HELLO negotiation.
# Reject duplicate fields and additional lines before opening a socket.
sub validate_request {
    my ($request) = @_;
    my $field = qr/ \ (?: MIN | MAX ) = $numeric_version /x;
    my ($operands) =
      $request =~ / \A HELLO\ VERSION ( (?:$field){1,2} ) $line_end /x;
    die "invalid SAM HELLO fixture\n" unless defined $operands;
    my %seen;
    for my $operand ( split / \  /x, $operands ) {
        next unless length $operand;
        my ($key) = split / = /x, $operand;
        die "invalid SAM HELLO fixture\n" if $seen{$key}++;
    }
    return;
}

sub send_request {
    my ( $socket, $request ) = @_;
    my $offset = 0;
    while ( $offset < length $request ) {
        my $sent = syswrite $socket, $request, length($request) - $offset,
          $offset;
        die "SAM send failed\n" if !defined $sent || $sent <= 0;
        $offset += $sent;
    }
    return;
}

# Do not overread into later protocol messages or retain an unbounded line.
sub read_reply {
    my ($socket) = @_;
    my $reply = '';
    while ( length($reply) < 8192 ) {
        my $count = sysread $socket, my $byte, 1;
        defined $count or die "SAM receive failed\n";
        last unless $count;
        $reply .= $byte;
        last if $byte eq "\n";
    }
    die "SAM reply exceeds limit\n"
      if length($reply) >= 8192 && $reply !~ / \n\z /x;
    length($reply) or die "SAM peer closed without a reply\n";
    return $reply;
}

my ( $host, $port, $path ) = @ARGV;
die "invalid loopback endpoint\n"
  if $host ne '127.0.0.1'
  || $port !~ / \A [0-9]{1,5} \z /x
  || $port <= 0
  || $port > 65535;
local $SIG{ALRM} = sub { die "SAM fixture deadline exceeded\n" };
alarm 10;
my $request = read_request($path);
validate_request($request);
my $socket = IO::Socket::INET->new(
    PeerAddr => $host,
    PeerPort => $port,
    Proto    => 'tcp',
    Timeout  => 10
) or die "SAM connection failed\n";
send_request( $socket, $request );
my $reply = read_reply($socket);
close $socket or die "SAM close failed\n";
alarm 0;

# Never expose MESSAGE or arbitrary fields from an untrusted peer.
my $result_code = qr/ OK | NOVERSION | I2P_ERROR /x;
my ( $result, $version ) = $reply =~ / \A HELLO\ REPLY\ RESULT=($result_code)
      (?: \ VERSION=($numeric_version) )? $line_end /x;
die "invalid SAM HELLO reply\n" unless defined $result;
$path !~ / [\x00-\x1f\x7f] /x or die "invalid SAM fixture label\n";
my $safe_reply = "HELLO REPLY RESULT=$result";
$safe_reply .= " VERSION=$version" if defined $version;
binmode STDOUT;
print "$path -> $safe_reply\n" or die "cannot write result\n";
exit 0;

__END__

=head1 NAME

sam-stream-send.pl - bounded loopback SAM HELLO fixture sender

=head1 CONTRACT

Accepts a numeric loopback port and one HELLO VERSION fixture with unique
MIN/MAX numeric operands. No session commands or credentials are accepted.
The complete file/connect/send/reply exchange has a ten-second deadline;
requests are capped at 64 KiB and replies at 8 KiB. Cancellation terminates
the process and closes its socket. Output preserves only the fixture label,
HELLO REPLY result, and optional numeric version. Exit zero means a valid
exchange, including NOVERSION or I2P_ERROR; the visible result describes
negotiation success. Malformed replies and local errors are nonzero.
Unexpected response fields are never printed.

=cut
