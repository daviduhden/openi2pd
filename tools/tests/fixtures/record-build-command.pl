#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: recording command double for make driver regressions.

use 5.016;
use strict;
use warnings;
use File::Basename qw(basename);
use JSON::PP;

my $name = basename($0);
die "MOCK_LOG is required\n"
  unless defined $ENV{MOCK_LOG} && length $ENV{MOCK_LOG};
open my $log, '>>', $ENV{MOCK_LOG} or die "mock log: $!\n";
print {$log} JSON::PP->new->canonical->encode(
    {
        name      => $name,
        args      => [@ARGV],
        destdir   => $ENV{DESTDIR}                    // '',
        parallel  => $ENV{CMAKE_BUILD_PARALLEL_LEVEL} // '',
        hardening => $ENV{DEB_BUILD_MAINT_OPTIONS}    // '',
    }
) . "\n";
close $log or die "mock log close: $!\n";
exit 23 if ( $ENV{MOCK_FAIL} // '' ) eq $name;

if ( $name eq 'dpkg-buildflags' ) {
    die "unexpected flags query\n" unless @ARGV == 2 && $ARGV[0] eq '--get';
    print $ENV{ 'MOCK_' . $ARGV[1] } // '';
}
exit 0;

__END__

=head1 NAME

record-build-command.pl - record a mock command without its side effects

=head1 CONTRACT

The test copies this file into its private command directory under the
names of build/package programs. C<MOCK_LOG> selects the owned JSON-lines
log. Arguments and the selected environment fields are recorded exactly;
no build, install, network operation or subprocess is executed.

C<MOCK_FAIL> selects a command name that exits 23. The dpkg-buildflags
double accepts only C<--get NAME> and returns C<MOCK_NAME>. Other doubles
return success after recording. Missing log authority or invalid flag
queries fail before simulating success. Requires Perl core modules only.

=cut
