#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check Android JNI declarations.
#
# Compile-free parity check across the three places a native entry point
# is declared: the Java `native` method, the checked-in generated JNI
# header and the C++ Java_* definition. It catches a renamed, missing or
# stale symbol before a device reports UnsatisfiedLinkError. It never
# builds, loads a library, runs a device or downloads anything.
#
# usage: check-android-jni-symbols.pl [--root DIR]

use strict;
use warnings;
use 5.016;
use Getopt::Long qw(GetOptions);
use File::Spec;
use File::Basename qw(dirname);
use Cwd            ();

my $root = Cwd::abs_path(
    File::Spec->catdir( dirname( File::Spec->rel2abs($0) ), '..', '..' ) );
GetOptions( 'root=s' => \$root ) or usage(2);
$root = Cwd::abs_path($root);
usage(2) unless defined $root;

my $java = File::Spec->catfile(
    $root,    'android', 'app', 'src',
    'main',   'java',    'org', 'openi2pd',
    'router', 'I2PD_JNI.java'
);
my $native = File::Spec->catfile( $root, 'android', 'app', 'src', 'main', 'cpp',
    'i2pd_android.cpp' );
my $header = File::Spec->catfile( $root, 'android', 'app', 'src', 'main', 'cpp',
    'org_openi2pd_router_I2PD_JNI.h' );

for my $path ( $java, $header, $native ) {
    usage(2) unless defined $path && -f $path;
}

sub slurp {
    my ($path) = @_;
    open my $fh, '<', $path or die "open $path: $!\n";
    local $/;
    my $text = <$fh>;
    close $fh;
    return $text // '';
}

sub usage {
    my ($code) = @_;
    print STDERR "usage: $0 [--root DIR]\n";
    exit $code;
}

my $java_text = slurp($java);

# A commented-out native declaration is not a symbol. Strip block and
# line comments before harvesting so a stale comment cannot create a
# phantom mismatch.
$java_text =~ s{/\*.*?\*/}{}gs;
$java_text =~ s{//[^\n]*}{}g;
my $native_text = slurp($native);

# The checked-in generated header is a third, independent list. A native
# added to Java and to the .cpp but not to the header no longer has a
# declared entry point, and a header declaration that Java dropped is a
# stale symbol that survives until the header is regenerated.
my $header_text = slurp($header);

my @java_names;
while ( $java_text =~ /(?:^|\s)native\s+[\w\.\[\]<>?]+\s+(\w+)\s*\(/mg ) {
    push @java_names, $1;
}
my %java = map { $_ => 1 } @java_names;

my %native;

# JNI mangles package/class separators as '_' and escapes a literal
# underscore as '_1', so I2PD_JNI appears as I2PD_1JNI. Only method
# names are compared: arguments, return types and overloads are outside
# this check.
while ( $native_text =~ /Java_org_openi2pd_router_I2PD_1JNI_(\w+)\s*\(/g ) {
    $native{$1} = 1;
}

my %header_symbols;
while ( $header_text =~ /Java_org_openi2pd_router_I2PD_1JNI_(\w+)\s*\(/g ) {
    $header_symbols{$1} = 1;
}

my $failed = 0;
for my $name ( sort @java_names ) {
    if ( !$header_symbols{$name} ) {
        print STDERR "missing header declaration for native $name\n";
        $failed = 1;
    }
    if ( !$native{$name} ) {
        print STDERR "missing C++ definition for native $name\n";
        $failed = 1;
    }
}
for my $name ( sort keys %native ) {
    if ( !$java{$name} ) {
        print STDERR "C++ definition $name has no Java native declaration\n";
        $failed = 1;
    }
}
for my $name ( sort keys %header_symbols ) {
    if ( !$java{$name} ) {
        print STDERR "header declaration $name has no Java native declaration\n";
        $failed = 1;
    }
}
if ($failed) {
    exit 1;
}
my $count = scalar @java_names;
print "Android JNI symbol parity: $count native declarations agree\n";
exit 0;

=head1 NAME

check-android-jni-symbols.pl - match Java native methods with the JNI
header and C++ definitions

=head1 SYNOPSIS

    perl android/tools/check-android-jni-symbols.pl [--root DIR]

DIR defaults to the repository containing the script. The Java declaration
file, the checked-in JNI header and the C++ source must all exist below it,
otherwise the usage text is printed and the script exits 2.

=head1 DESCRIPTION

Compile-free parity check between the C<native> declarations in
F<android/app/src/main/java/org/openi2pd/router/I2PD_JNI.java>, the
declarations in F<android/app/src/main/cpp/org_openi2pd_router_I2PD_JNI.h>
and the C<Java_...> functions in
F<android/app/src/main/cpp/i2pd_android.cpp>. A missing definition is the
static form of a runtime C<UnsatisfiedLinkError>; a header declaration or a
definition without a matching Java method is a stale or renamed symbol. All
directions are reported.

Java block and line comments are stripped first, because a commented-out
declaration is not a symbol and would otherwise create a phantom mismatch.
Method names are compared as a set: overloads, parameter types, return types
and the C<noexcept> specifier are outside this check.

=head1 OUTPUT AND EXIT STATUS

Each mismatch is printed on STDERR as one line naming the method, and the exit
status is 1. A clean run prints one summary line on STDOUT and exits 0. Bad
arguments, an unresolvable root or a missing input file print the usage line
on STDERR and exit 2.

=head1 FILE FORMAT

Textual scanning of three source files; no compiler, Gradle task, header
generator, shared library or device is involved.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, Getopt::Long, File::Spec, File::Basename,
Cwd, 5.016). It runs with the OpenBSD base interpreter.

=head1 SECURITY AND PORTABILITY

Read-only and offline: nothing is built, loaded or downloaded, and no Android
device is contacted. The check is platform-independent.
