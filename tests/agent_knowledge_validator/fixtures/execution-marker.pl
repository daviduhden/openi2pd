#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
# Git may append hook/driver operands. Never evaluate those as code.
use 5.016;
use strict;
use warnings;
my $marker = "$0.marker";
open my $file, '>', $marker or die "cannot create execution marker\n";
print {$file} "executed\n" or die "cannot write execution marker\n";
close $file or die "cannot close execution marker\n";
print "contract-token\0" or die "cannot write fixture output\n";
