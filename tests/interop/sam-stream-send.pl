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
my ($host, $port, $path) = @ARGV;
$host eq '127.0.0.1' && $port =~ /\A[0-9]{1,5}\z/
  && $port > 0 && $port <= 65535 or die "invalid loopback endpoint\n";
local $SIG{ALRM} = sub { die "SAM fixture deadline exceeded\n" };
alarm 10;
open my $input, '<:raw', $path or die "cannot open SAM fixture\n";
my $request = '';
while (1) {
    my $count = read $input, my $chunk, 8192;
    defined $count or die "cannot read SAM fixture\n";
    last unless $count;
    $request .= $chunk;
    length($request) <= 65536 or die "SAM fixture exceeds limit\n";
}
close $input or die "cannot close SAM fixture\n";
# This diagnostic client accepts only credential-free HELLO negotiation.
# Reject duplicate fields and additional lines before opening a socket.
$request =~ /\AHELLO VERSION((?: (?:MIN|MAX)=[0-9]+(?:\.[0-9]+)*){1,2})\r?\n\z/
  or die "invalid SAM HELLO fixture\n";
my %fields;
for my $field (split / /, $1) {
    next unless length $field;
    my ($key) = split /=/, $field;
    !$fields{$key}++ or die "invalid SAM HELLO fixture\n";
}
my $socket = IO::Socket::INET->new(
    PeerAddr => $host, PeerPort => $port, Proto => 'tcp', Timeout => 10
) or die "SAM connection failed\n";
my $offset = 0;
while ($offset < length $request) {
    my $sent = syswrite $socket, $request, length($request)-$offset, $offset;
    defined $sent && $sent > 0 or die "SAM send failed\n";
    $offset += $sent;
}
# Do not overread into later protocol messages or retain an unbounded line.
my $reply = '';
while (length($reply) < 8192) {
    my $count = sysread $socket, my $byte, 1;
    defined $count or die "SAM receive failed\n";
    last unless $count;
    $reply .= $byte;
    last if $byte eq "\n";
}
length($reply) < 8192 || $reply =~ /\n\z/
  or die "SAM reply exceeds limit\n";
length($reply) or die "SAM peer closed without a reply\n";
close $socket or die "SAM close failed\n";
alarm 0;
# Never expose MESSAGE or arbitrary fields from an untrusted peer.
$reply =~ /\AHELLO REPLY RESULT=(OK|NOVERSION|I2P_ERROR)(?: VERSION=([0-9]+(?:\.[0-9]+)*))?\r?\n\z/
  or die "invalid SAM HELLO reply\n";
my ($result, $version) = ($1, $2);
$path !~ /[\x00-\x1f\x7f]/ or die "invalid SAM fixture label\n";
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
