#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check vectors.
#
# Validates checked-in interoperability fixtures and provenance references, and can optionally run a user-supplied C++ harness per case; it never downloads, writes the reference tree or builds Java.
#

use strict;
use warnings;
use Getopt::Long qw(GetOptions);
use Digest::SHA qw(sha256_hex);
use JSON::PP;
use File::Find qw(find);
use File::Spec;
use File::Basename qw(dirname);

my $TAG = 'i2p-2.13.0';
my $COMMIT = '9134f808337b401e8e53c73734c81fab04280c9d';
my $SCRIPT_DIR = dirname(File::Spec->rel2abs($0));

my ($reference, $binary, $vectors, $case, $list, $require_reference, $help);
$reference = $ENV{I2P_JAVA_REFERENCE} if defined $ENV{I2P_JAVA_REFERENCE};

GetOptions(
    'reference=s'         => \$reference,
    'binary=s'            => \$binary,
    'vectors=s'           => \$vectors,
    'case=s'              => \$case,
    'list'                => \$list,
    'require-reference'   => \$require_reference,
    'help|h'              => \$help,
) or usage(2);

if ($help) { usage(0); }

$vectors = File::Spec->catdir($SCRIPT_DIR, 'vectors') unless defined $vectors;
$vectors = File::Spec->rel2abs($vectors);

sub usage {
    my ($code) = @_;
    my $name = File::Spec->abs2rel($0, $SCRIPT_DIR);
    print STDERR <<"USAGE";
usage: $name [options]

  --reference DIR        validate the pinned I2P Java checkout (default:
                         \$I2P_JAVA_REFERENCE, if set)
  --require-reference    fail when no reference checkout is available
  --binary PATH          run a C++ vector harness: PATH --case ID INPUT
  --vectors DIR          fixture root (default: tests/interop/vectors)
  --case ID              select one case
  --list                 list cases without reading fixtures
  --help                 this text

Without --binary the script only validates checked-in fixtures (hashes,
field sizes, I2NP checksums, I2CP length framing) and provenance line
references.  It prints "not executed" for every differential case.
USAGE
    exit $code;
}

sub run_git {
    my (@args) = @_;
    open my $fh, '-|', 'git', '-C', $reference, @args or return (undef, "git: $!");
    local $/;
    my $out = <$fh>;
    close $fh;
    $out = '' unless defined $out;
    return ($out, undef);
}

sub validate_reference {
    return 0 unless defined $reference && length $reference;
    if (!-d File::Spec->catdir($reference, '.git')) {
        print STDERR "not a git checkout: $reference\n";
        return 2;
    }
    my ($commit, $err) = run_git('rev-parse', 'HEAD');
    if (defined $err) { print STDERR "$err\n"; return 2; }
    chomp $commit;
    if ($commit ne $COMMIT) {
        print STDERR "unexpected reference commit: $commit\n";
        return 2;
    }
    my ($tag, $terr) = run_git('describe', '--tags', '--exact-match');
    if (defined $terr) { print STDERR "$terr\n"; return 2; }
    chomp $tag;
    if ($tag ne $TAG) {
        print STDERR "unexpected reference tag: ", ($tag eq '' ? '<detached>' : $tag), "\n";
        return 2;
    }
    my ($status, $serr) = run_git('status', '--porcelain');
    if (defined $serr) { print STDERR "$serr\n"; return 2; }
    if ($status ne '') {
        print STDERR "reference tree is not clean\n";
        return 2;
    }
    my $writable = -w $reference ? 1 : 0;
    if ($writable) {
        print STDERR "warning: reference tree is writable; chmod -R a-w is recommended\n";
    }
    print "reference: $TAG $COMMIT clean read-only=", ($writable ? 0 : 1), "\n";
    return 1;
}

sub read_input {
    my ($path, $format) = @_;
    open my $fh, '<', $path or die "open $path: $!\n";
    binmode $fh;
    local $/;
    my $raw = <$fh>;
    close $fh;
    $raw = '' unless defined $raw;
    return $raw if $format ne 'hex';
    $raw =~ s/^[ \t]*#.*$//mg;
    $raw =~ s/\s+//g;
    die "odd hex length in $path\n" if length($raw) % 2;
    die "non-hex character in $path\n" unless $raw =~ /\A[0-9a-fA-F]*\z/;
    return pack 'H*', $raw;
}

sub load_cases {
    my @found;
    find(
        sub {
            return unless -f $_ && /\.case\.json\z/;
            push @found, File::Spec->rel2abs($_);
        },
        $vectors,
    );
    my @cases;
    for my $path (sort @found) {
        open my $fh, '<', $path or die "open $path: $!\n";
        local $/;
        my $text = <$fh>;
        close $fh;
        my $data = eval { JSON::PP->new->decode($text) };
        die "invalid JSON in $path: $@" unless defined $data;
        $data->{_path} = $path;
        $data->{_dir} = dirname($path);
        push @cases, $data;
    }
    die "no .case.json files under $vectors\n" unless @cases;
    return @cases;
}

sub check_provenance {
    my ($c, $ref) = @_;
    my @errors;
    my $prov = $c->{provenance};
    if (ref($prov) ne 'HASH' || ref($prov->{java}) ne 'ARRAY' || !@{$prov->{java}}) {
        push @errors, 'missing provenance.java entries';
        return @errors;
    }
    if (ref($prov->{spec}) ne 'ARRAY' || !@{$prov->{spec}}) {
        push @errors, 'missing provenance.spec entries';
    }
    return @errors unless defined $ref && length $ref;
    for my $j (@{$prov->{java}}) {
        my $p = File::Spec->catfile($ref, $j->{path});
        if (!-f $p) {
            push @errors, "provenance file missing: $j->{path}";
            next;
        }
        open my $fh, '<', $p or do { push @errors, "open $p: $!"; next; };
        my $line;
        for (my $i = 1; $i <= $j->{line}; ++$i) { $line = <$fh>; }
        close $fh;
        if (!defined $line) {
            push @errors, "provenance line $j->{line} past EOF: $j->{path}";
            next;
        }
        chomp $line;
        if (index($line, $j->{contains}) < 0) {
            push @errors, "provenance mismatch $j->{path}:$j->{line}: expected '$j->{contains}'";
        }
    }
    return @errors;
}

sub static_checks {
    my ($c, $bytes) = @_;
    my @errors;
    my $kind = $c->{kind} // '';
    my $expectation = $c->{expectation} // '';
    push @errors, 'format must be hex or raw' unless ($c->{format} // '') =~ /\A(?:hex|raw)\z/;
    if ($expectation ne 'accept' && $expectation ne 'reject') {
        push @errors, 'expectation must be accept or reject';
    }
    if ($kind eq 'i2np') {
        if (length($bytes) < 16) {
            push @errors, 'shorter than the 16-byte I2NP header';
        } else {
            my ($type, $msgid, $exp, $size, $chk) = unpack 'C N Q> n C', substr($bytes, 0, 16);
            my $payload = substr($bytes, 16);
            if ($expectation eq 'accept') {
                push @errors, "declared size $size != payload " . length($payload) if $size != length($payload);
                my $want = hex substr(sha256_hex($payload), 0, 2);
                push @errors, sprintf('checksum %02x != SHA-256 prefix %02x', $chk, $want) if $chk != $want;
            }
            my $ev = $c->{expect}{value};
            if ($expectation eq 'accept' && ref($ev) eq 'HASH') {
                push @errors, "expect.value.type $ev->{type} != header type $type" if defined $ev->{type} && $ev->{type} != $type;
                push @errors, "expect.value.size $ev->{size} != header size $size" if defined $ev->{size} && $ev->{size} != $size;
                push @errors, "expect.value.message_id $ev->{message_id} != header id $msgid" if defined $ev->{message_id} && $ev->{message_id} != $msgid;
                push @errors, "expect.value.expiration $ev->{expiration} != header expiration $exp" if defined $ev->{expiration} && $ev->{expiration} != $exp;
            }
        }
    } elsif ($kind eq 'i2cp') {
        if (length($bytes) < 5) {
            push @errors, 'shorter than the 5-byte I2CP length/type header';
        } else {
            my ($len, $type) = unpack 'N C', substr($bytes, 0, 5);
            if ($expectation eq 'accept' && 5 + $len != length($bytes)) {
                push @errors, "I2CP length $len does not match " . (length($bytes) - 5) . ' body bytes';
            }
            my $ev = $c->{expect}{value};
            if ($expectation eq 'accept' && ref($ev) eq 'HASH') {
                push @errors, "expect.value.type $ev->{type} != I2CP type $type" if defined $ev->{type} && $ev->{type} != $type;
                push @errors, "expect.value.length $ev->{length} != I2CP length $len" if defined $ev->{length} && $ev->{length} != $len;
            }
        }
    } elsif ($kind eq 'sam') {
        push @errors, 'SAM fixtures must be raw text' if $c->{format} ne 'raw';
    } else {
        push @errors, "unknown kind '$kind'";
    }
    return @errors;
}

sub canonical {
    my ($data) = @_;
    return JSON::PP->new->canonical->encode($data);
}

sub run_binary {
    my ($bin, $c, $input) = @_;
    open my $ph, '-|', $bin, '--case', $c->{id}, $input
        or return (undef, "cannot execute $bin: $!");
    local $/;
    my $out = <$ph>;
    close $ph;
    my $status = $? >> 8;
    return (undef, "harness exited $status") if $status != 0;
    my $got = eval { JSON::PP->new->decode($out) };
    return (undef, 'harness output is not JSON: ' . ($@ =~ s/\s+\z//r)) unless defined $got;
    return ($got, undef);
}

my $ref_state = 0;
if (defined $reference && length $reference) {
    $ref_state = validate_reference();
    exit 2 if $ref_state == 2;
} elsif ($require_reference) {
    print STDERR "no reference checkout: set I2P_JAVA_REFERENCE or pass --reference\n";
    exit 2;
} else {
    print "reference: not supplied; skipping tag/commit and provenance line checks\n";
}

my @cases = load_cases();
@cases = grep { $_->{id} eq $case } @cases if defined $case && length $case;
if (!@cases) {
    print STDERR "no matching cases\n";
    exit 2;
}

if ($list) {
    for my $c (@cases) {
        printf "%-38s %-9s %-7s %s\n", $c->{id}, $c->{kind}, $c->{expectation},
            (defined $c->{known_gap} ? 'known-gap' : '');
    }
    exit 0;
}

my ($ok, $failed, $known, $unexpected_pass, $executed) = (0, 0, 0, 0, 0);

for my $c (@cases) {
    my @errors;
    for my $field (qw(id kind input format sha256 expectation expect provenance)) {
        push @errors, "missing field $field" unless exists $c->{$field};
    }
    if (@errors) {
        printf "FAIL %s\n", $c->{id} // $c->{_path};
        print "  - $_\n" for @errors;
        ++$failed;
        next;
    }
    my $input = File::Spec->catfile($c->{_dir}, $c->{input});
    my $bytes;
    eval { $bytes = read_input($input, $c->{format}); 1 } or do {
        printf "FAIL %s\n  - %s\n", $c->{id}, $@;
        ++$failed;
        next;
    };
    my $sha = sha256_hex($bytes);
    push @errors, "sha256 $sha != manifest $c->{sha256}" if $sha ne $c->{sha256};
    push @errors, check_provenance($c, $ref_state ? $reference : undef);
    push @errors, static_checks($c, $bytes);

    if (@errors) {
        printf "FAIL %s\n", $c->{id};
        print "  - $_\n" for @errors;
        ++$failed;
        next;
    }

    if (!defined $binary || !length $binary) {
        printf "STATIC-OK %-38s %s\n", $c->{id}, (defined $c->{known_gap} ? '[known gap]' : '');
        if (defined $c->{known_gap}) { ++$known; } else { ++$ok; }
        next;
    }

    if (!-x $binary) {
        printf "FAIL %s\n  - harness is not executable: %s\n", $c->{id}, $binary;
        ++$failed;
        next;
    }

    my ($got, $err) = run_binary($binary, $c, $input);
    ++$executed;
    if (defined $err) {
        printf "FAIL %s\n  - %s\n", $c->{id}, $err;
        ++$failed;
        next;
    }
    my $same = canonical($got) eq canonical($c->{expect});
    if ($same) {
        if (defined $c->{known_gap}) {
            printf "UNEXPECTED-PASS %-30s %s\n", $c->{id}, $c->{known_gap};
            ++$unexpected_pass;
        } else {
            printf "PASS %-38s\n", $c->{id};
            ++$ok;
        }
    } elsif (defined $c->{known_gap}) {
        printf "KNOWN-GAP %-34s %s\n", $c->{id}, $c->{known_gap};
        ++$known;
    } else {
        printf "FAIL %s\n", $c->{id};
        print "  expected: ", canonical($c->{expect}), "\n";
        print "  actual:   ", canonical($got), "\n";
        ++$failed;
    }
}

printf "\n%d case(s): %d static/vector ok, %d failed, %d known gap, %d unexpected pass\n",
    scalar(@cases), $ok, $failed, $known, $unexpected_pass;
if (!defined $binary || !length $binary) {
    print "differential cases not executed: no --binary supplied\n";
} else {
    print "differential cases executed: $executed\n";
}
exit($failed ? 1 : 0);
