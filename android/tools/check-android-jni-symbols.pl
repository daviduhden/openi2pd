#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check Android JNI declarations.
#
# Compile-free parity check between the Java native declarations and the C++
# Java_* definitions. It catches a renamed or
# missing native method before a device reports UnsatisfiedLinkError. It never
# builds, loads a library, runs a device or downloads anything.
#
# usage: check-android-jni-symbols.pl [--root DIR]

use strict;
use warnings;
use 5.016;
use Getopt::Long qw(GetOptions);
use File::Spec;
use File::Basename qw(dirname);
use Cwd ();

my $root = Cwd::abs_path(File::Spec->catdir(dirname(File::Spec->rel2abs($0)), '..', '..'));
GetOptions('root=s' => \$root) or usage(2);
$root = Cwd::abs_path($root);
usage(2) unless defined $root;

my $java = File::Spec->catfile($root, 'android', 'app', 'src', 'main', 'java',
    'org', 'openi2pd', 'router', 'I2PD_JNI.java');
my $native = File::Spec->catfile($root, 'android', 'app', 'src', 'main', 'cpp', 'i2pd_android.cpp');

for my $path ($java, $native) {
    usage(2) unless defined $path && -f $path;
}

sub slurp
{
    my ($path) = @_;
    open my $fh, '<', $path or die "open $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    return $text // '';
}

sub usage
{
    my ($code) = @_;
    print STDERR "usage: $0 [--root DIR]\n";
    exit $code;
}

my $java_text = slurp($java);
# A commented-out native declaration is not a symbol. Strip block and line
# comments before harvesting so a stale comment cannot create a phantom
# mismatch.
$java_text =~ s{/\*.*?\*/}{}gs;
$java_text =~ s{//[^\n]*}{}g;
my $native_text = slurp($native);

my @java_names;
while ($java_text =~ /(?:^|\s)native\s+[\w\.\[\]<>?]+\s+(\w+)\s*\(/mg) {
    push @java_names, $1;
}
my %java = map { $_ => 1 } @java_names;

my %native;
while ($native_text =~ /Java_org_openi2pd_router_I2PD_1JNI_(\w+)\s*\(/g) {
    $native{$1} = 1;
}

my $failed = 0;
for my $name (sort @java_names) {
    if (!$native{$name}) {
        print STDERR "missing C++ definition for native $name\n";
        $failed = 1;
    }
}
for my $name (sort keys %native) {
    if (!$java{$name}) {
        print STDERR "C++ definition $name has no Java native declaration\n";
        $failed = 1;
    }
}
if ($failed) {
    exit 1;
}
my $count = scalar @java_names;
print "Android JNI symbol parity: $count native declarations agree\n";
exit 0;
