#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check Android JNI declarations.
#
# Compile-free parity check across the three places a native entry point
# is declared: the Java `native` method, the checked-in generated JNI
# header and the C++ Java_* definition. It catches a renamed, missing or
# stale symbol or an ABI mismatch before the Android device job. It never
# builds, loads a library, runs a device or downloads anything.
#
# usage: check-android-jni-symbols.pl [--root DIR] [--generated-header FILE]

use strict;
use warnings;
use 5.016;
use Getopt::Long qw(GetOptions);
use File::Spec;
use File::Basename qw(dirname);
use Cwd            ();

my $root = Cwd::abs_path(
    File::Spec->catdir( dirname( File::Spec->rel2abs($0) ), '..', '..' ) );
my $generated_header;
GetOptions( 'root=s' => \$root, 'generated-header=s' => \$generated_header )
  or usage(2);
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

usage(2) if defined($generated_header) && !-f $generated_header;

sub slurp {
    my ($path) = @_;
    open my $fh, '<', $path or die "open $path: $!\n";
    local $/ = undef;
    my $text = <$fh>;
    close $fh;
    return $text // '';
}

sub usage {
    my ($code) = @_;
    print STDERR "usage: $0 [--root DIR] [--generated-header FILE]\n";
    exit $code;
}

my $failed = 0;

sub mismatch {
    my ($diagnostic) = @_;
    print STDERR "$diagnostic\n";
    $failed = 1;
    return;
}

sub source_without_comments {
    my ($path) = @_;
    my $source = slurp($path);
    $source =~ s{/\*.*?\*/}{ }gsx;
    $source =~ s{//[^\n]*}{}gx;
    return $source;
}

# Keep the set of Java spellings closed. An unfamiliar type requires
# deliberate mapping so a source change cannot evade the ABI check.
my %primitive =
  map { $_ => "j$_" } qw(boolean byte char short int long float double);

sub jni_type {
    my ($type) = @_;
    $type =~ s/\s+//gx;
    return $primitive{$type} if exists $primitive{$type};
    return 'void'            if $type eq 'void';
    return 'jstring'         if $type eq 'String';
    return 'jclass'          if $type eq 'Class<?>';

    # C JNI erases object-array element types. This bridge reads byte
    # arrays from its object-array slots, so other element types need
    # explicit review even though javac emits the same JNI C type.
    return 'jobjectArray' if $type eq 'byte[][]';
    if ( $type =~ /\A(\w+)\[\]\z/x ) {
        my $element = $1;
        return $primitive{$element} . 'Array' if exists $primitive{$element};
    }
    return;
}

sub record_signature {
    my ( $entries, $label, $name, $signature, $noexcept ) = @_;
    if ( exists $entries->{$name} ) {
        mismatch(
            "duplicate $label for native $name; overloads require ABI review");
        return;
    }
    $entries->{$name} = {
        abi      => join( ', ', @{$signature} ),
        noexcept => $noexcept,
    };
    return;
}

sub java_signatures {
    my ($source) = @_;
    my %entries;

    # Consume quoted and comment tokens together so a URL inside a
    # string is never mistaken for the start of a line comment.
    my $comment    = qr{ // [^\n]* | /\* .*? \*/ }sx;
    my $text_block = qr{ """ (?: \\. | (?!"""). )* """ }sx;
    my $string     = qr{ " (?: [^"\\\n] | \\. )* " }x;
    my $character  = qr{ ' (?: [^'\\\n] | \\. )* ' }x;
    $source =~ s{ $comment | $text_block | $string | $character }{ }gx;
    my $modifier =
      qr{ public | protected | private | static | final | synchronized }x;
    my $java_type = qr{ [\w<>\[\]\s?]+? }x;

    while (
        $source =~ /
        ((?: $modifier \s+ )*)
        \bnative \s+ ($java_type) \s+ (\w+) \s* \(([^()]*)\) \s* ;
    /gx
      )
    {
        my ( $modifiers, $return_type, $name, $parameters ) =
          ( $1, $2, $3, $4 );
        my @signature = (
            jni_type($return_type), 'JNIEnv*',
            $modifiers =~ /\bstatic\b/x ? 'jclass' : 'jobject'
        );
        for my $parameter ( split /,/x, $parameters, -1 ) {
            if ( $parameter =~ /\A\s*([\w<>\[\]\s?]+?)\s+\w+\s*\z/x ) {
                my $type = jni_type($1);
                push @signature,
                  defined($type) && $type ne 'void' ? $type : undef;
            }
            else {
                push @signature, undef;
            }
        }
        if ( grep { !defined } @signature ) {
            mismatch("unsupported Java signature for native $name");
            next;
        }
        record_signature( \%entries, 'Java declaration', $name, \@signature );
    }

    # Reject unsupported declaration syntax rather than dropping it.
    my $native_count = () = $source =~ /\bnative\b/gx;
    if ( !$native_count || $native_count != keys %entries ) {
        mismatch('could not account for every Java native declaration');
    }
    return \%entries;
}

sub cpp_signatures {
    my ( $source, $label, $terminator ) = @_;
    my %entries;
    my $jni_export = qr{ \bJNIEXPORT \s+ (\w+) \s+ JNICALL \s+ }x;
    my $jni_symbol = qr{ Java_org_openi2pd_router_I2PD_1JNI_(\w+) }x;
    while (
        $source =~ /
        $jni_export $jni_symbol \s*
        \(([^()]*)\) \s* (noexcept\s*)? \Q$terminator\E
    /gx
      )
    {
        my ( $return_type, $name, $parameters, $noexcept ) = ( $1, $2, $3, $4 );
        my @signature = ($return_type);
        for my $parameter ( split /,/x, $parameters, -1 ) {
            if ( $parameter =~ /\A\s*(JNIEnv\s*\*|\w+)(?:\s*\b\w+)?\s*\z/x ) {
                my $type = $1;
                $type =~ s/\s+//gx;
                push @signature, $type;
            }
            else {
                mismatch("unsupported $label parameter for native $name");
            }
        }
        record_signature( \%entries, $label, $name, \@signature,
            defined($noexcept) ? 1 : 0 );
    }
    return \%entries;
}

sub compare_signatures {
    my ( $java_signatures, $native_signatures, $label ) = @_;
    for my $name ( sort keys %{$java_signatures} ) {
        if ( !exists $native_signatures->{$name} ) {
            mismatch("missing $label for native $name");
            next;
        }
        my $expected = $java_signatures->{$name}{abi};
        my $actual   = $native_signatures->{$name}{abi};
        mismatch(
"$label signature mismatch for native $name (expected $expected; found $actual)"
        ) if $expected ne $actual;
    }
    for my $name ( sort keys %{$native_signatures} ) {
        mismatch("$label $name has no Java native declaration")
          if !exists $java_signatures->{$name};
    }
    return;
}

my $java_entries = java_signatures( slurp($java) );
my $header_entries =
  cpp_signatures( source_without_comments($header), 'header declaration', ';' );
my $native_entries =
  cpp_signatures( source_without_comments($native), 'C++ definition', '{' );
compare_signatures( $java_entries, $header_entries, 'header declaration' );
compare_signatures( $java_entries, $native_entries, 'C++ definition' );
if ( defined $generated_header ) {
    compare_signatures(
        $java_entries,
        cpp_signatures(
            source_without_comments($generated_header),
            'javac header declaration', ';'
        ),
        'javac header declaration'
    );
}
for my $name ( sort keys %{$header_entries} ) {
    next if !exists $native_entries->{$name};
    mismatch("C++ exception specification mismatch for native $name")
      if $header_entries->{$name}{noexcept} !=
      $native_entries->{$name}{noexcept};
}

exit 1 if $failed;
my $count = keys %{$java_entries};
print "Android JNI symbol parity: $count native declarations agree\n";
exit 0;

=head1 NAME

check-android-jni-symbols.pl - match Java native methods with the JNI
header and C++ definitions

=head1 SYNOPSIS

    perl android/tools/check-android-jni-symbols.pl [--root DIR] [--generated-header FILE]

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

Comments are stripped before scanning. Return and parameter types, argument
order, and the static C<jclass> or instance C<jobject> receiver must agree.
The checked-in header and C++ definitions must also agree on C<noexcept>.
With C<--generated-header>, the declarations from the debug Java compiler
are checked as a separate input. Overloads and unfamiliar Java type syntax
require explicit support in the checker.

=head1 OUTPUT AND EXIT STATUS

Each mismatch is printed on STDERR as one line naming the method, and the exit
status is 1. A clean run prints one summary line on STDOUT and exits 0. Bad
arguments, an unresolvable root or a missing input file print the usage line
on STDERR and exit 2.

=head1 FILE FORMAT

Textual scanning of three source files and an optional generated header.
The checker itself does not invoke a compiler or load a library.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, Getopt::Long, File::Spec, File::Basename,
Cwd, 5.016). It runs with the OpenBSD base interpreter.

=head1 SECURITY AND PORTABILITY

Read-only and offline: nothing is built, loaded or downloaded, and no Android
device is contacted. The check is platform-independent.
