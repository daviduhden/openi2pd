#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright and
# license details.
#
# openi2pd: check Android Javadoc comments.
#
# Compile-free Javadoc contract for the manual source-docs lane. The
# Gradle `sourceJavadoc` task runs javadoc 25 with
# `-Xdoclint:all,-missing -Werror`, so a malformed comment fails the
# Android job long after the interesting part of the run. This checker
# reproduces the doclint classes that a comment review can decide
# locally: unregistered block tags (for example `@implNote`, which is a
# JDK-internal tag, not a standard doclet tag), raw angle-bracket
# placeholders in prose that javadoc parses as HTML element names, and a
# documented parameter that the following declaration does not have.
# It never runs Gradle, javadoc or a device.
#
# usage: check-android-javadoc.pl [--root ANDROID_DIRECTORY]

use strict;
use warnings;
use 5.016;
use File::Find qw(find);
use File::Spec;
use Getopt::Long qw(GetOptions);

# Standard doclet block tags. Anything else at the start of a comment
# line is an "unknown tag" error under -Xdoclint:all.
my %block_tag = map { $_ => 1 }
  qw(author deprecated exception hidden param provides return see serial
  serialData serialField since throws uses version);

# Inline tags are only valid inside braces. Reaching the start of a line
# means the comment used one as a block tag, which javadoc rejects.
my %inline_tag = map { $_ => 1 }
  qw(code docRoot inheritDoc link linkplain literal value);

# Javadoc-supported HTML element names. The list is deliberately
# generous: the checker exists to reject placeholder text such as
# `<tunnel>` or `<id>`, not to police uncommon but valid HTML.
my %html_tag = map { $_ => 1 } qw(
  a abbr address area article aside b bdi bdo big blockquote br
  caption center cite code col colgroup dd del details dfn div dl dt
  em figcaption figure font footer h1 h2 h3 h4 h5 h6 header hr i img
  ins kbd li main mark nav ol p pre q s samp section small span strike
  strong sub summary sup table tbody td tfoot th thead tr tt u ul var
  wbr
);

my $root_arg;
GetOptions( 'root=s' => \$root_arg )
  or die "usage: $0 [--root ANDROID_DIRECTORY]\n";
die "usage: $0 [--root ANDROID_DIRECTORY]\n" if @ARGV;
my $root = defined $root_arg ? $root_arg
  : -d 'android/app/src/main' ? 'android'
  : '.';
my $java_root = File::Spec->catdir( $root, qw(app src main java) );
if ( !-d $java_root ) {
    print "FAIL: no Java source tree under $java_root\n";
    exit 1;
}

my @errors;

sub fail {
    my @parts = @_;
    return push @errors, join( '', @parts ) . "\n";
}

sub slurp {
    my ($path) = @_;
    open( my $fh, '<:raw', $path ) or die "open $path: $!";
    local $/ = undef;
    my $text = <$fh>;
    close($fh);
    return $text;
}

# Remove the inline tags that switch javadoc out of HTML parsing. Both
# {@code} and {@literal} render their body verbatim, so `<tunnel>` in
# them is text; {@link} and friends carry a reference rather than
# rendered content.
sub strip_inline_tags {
    my ($text) = @_;
    $text =~ s/\{\@(?:code|literal)\s+[^}]*\}//gs;
    $text =~ s/\{\@(?:link|linkplain|value|inheritDoc|docRoot)[^}]*\}//gs;
    return $text;
}

# Collect the declaration that a comment documents: annotations and
# comment lines are skipped, then text is gathered until the end of the
# statement or the opening brace of a body.
sub declaration_after {
    my ( $lines, $from ) = @_;
    my @decl;
    my $depth = 0;
    for ( my $i = $from; $i < @$lines && @decl < 40; $i++ ) {
        my $line = $lines->[$i];
        next if $line =~ m{^\s*(?:\@|//|/\*|\*|\})};
        push @decl, $line;
        $depth += ( $line =~ tr/(// ) - ( $line =~ tr/)// );
        last if $depth <= 0 && $line =~ /[;{]/;
    }
    my $decl = join( '', @decl );
    $decl =~ s/\s+/ /g;
    return $decl;
}

# Parameter names of the declaration, or an empty list when the
# declaration has no parameter list this checker can trust.
sub declared_params {
    my ($decl) = @_;
    return () unless $decl =~ /\(/;
    my $args = $decl;
    $args =~ s/^[^(]*\(//;
    $args =~ s/\).*$//;
    my ( @names, $current, $depth );
    $depth = 0;
    for my $char ( split //, $args ) {
        if ( $char =~ /[<(]/ ) { $depth++ }
        elsif ( $char =~ /[>)]/ ) { $depth-- }
        if ( $char eq ',' && $depth == 0 ) {
            push @names, $current;
            $current = '';
            next;
        }
        $current .= $char;
    }
    push @names, $current if length $current;
    my @parsed;
    for my $param (@names) {
        $param =~ s/^\s+|\s+$//g;
        next if $param eq '';
        $param =~ s/=.*$//;
        $param =~ s/^\@\w+(?:\([^)]*\))?\s+//;
        $param =~ s/^final\s+//;
        $param =~ s/\s*\[\s*\]$//;
        $param =~ s/\.\.\.$//;
        $param =~ s/^.*\s//;
        push @parsed, $param
          if $param =~ /^[A-Za-z_]\w*$/ && $param !~ /^(?:void|final)$/;
    }
    return @parsed;
}

sub check_block {
    my ( $file, $start, $block, $file_lines, $after ) = @_;
    my $decl = declaration_after( $file_lines, $after );
    my $where = "$file line " . ( $start + 1 );

    my $text = strip_inline_tags( join( '', @$block ) );
    for my $raw ( split /\n/, $text, -1 ) {

        # Unregistered block tag, for example a JDK-internal @implNote.
        if ( $raw =~ /^\s*\*\s*\@([A-Za-z]\w*)/ ) {
            my $tag = $1;
            if ( !$block_tag{$tag} ) {
                my $hint = $inline_tag{$tag}
                  ? 'inline tag used as a block tag; wrap it in braces'
                  : 'unregistered tag; use plain prose or a standard tag';
                fail("$where: unknown tag \@$tag ($hint)");
            }
        }

        # Raw placeholder such as <tunnel>, <id> or List<String> in
        # prose. Angle brackets are only safe inside {@code}/{@literal}
        # or when they are a supported HTML element. A tag name must
        # follow the bracket immediately; `< iExp` is plain text.
        while ( $raw =~ m{</?([A-Za-z][A-Za-z0-9]*)}g ) {
            my $tag = lc $1;
            next if $html_tag{$tag};
            fail( "$where: <$1> is parsed as HTML, not text; use "
                  . "{\@code <$1>} or &lt;$1&gt;" );
        }
    }

    my @documented;
    for my $raw ( @$block[ 1 .. $#$block ] ) {
        while ( $raw =~ /\@param\s+(<[^>]+>|\w+)/g ) {
            my $name = $1;
            next if $name =~ /^</;    # @param <T> documents a type
            push @documented, $name;
        }
    }
    return unless @documented;
    my @declared = declared_params($decl);
    return unless @declared;
    my %declared = map { $_ => 1 } @declared;
    for my $name (@documented) {
        if ( !$declared{$name} && $decl !~ /\b\Q$name\E\b/ ) {
            fail("$where: \@param $name is not a parameter of "
                  . "'$decl'");
        }
    }
    return;
}

my @files;
find(
    sub {
        return unless /\.java\z/;
        push @files, $File::Find::name;
    },
    $java_root
);
@files = sort @files;

for my $file (@files) {
    my $text = slurp($file);
    my @lines = split /\n/, $text, -1;
    for ( my $i = 0; $i < @lines; $i++ ) {
        next unless $lines[$i] =~ m{^\s*/\*\*};
        my @block;
        my $j = $i;
        while ( $j < @lines ) {
            push @block, $lines[$j] . "\n";
            last if $lines[$j] =~ m{\*/};
            $j++;
        }
        check_block( $file, $i, \@block, \@lines, $j + 1 );
        $i = $j;
    }
}

if (@errors) {
    print STDERR $_ for @errors;
    printf STDERR "FAIL: %d Android Javadoc defect(s)\n", scalar @errors;
    exit 1;
}
printf "OK: no Javadoc defects in %d Android source file(s)\n",
  scalar @files;
exit 0;

=head1 NAME

check-android-javadoc.pl - verify the Android Javadoc comments

=head1 SYNOPSIS

    perl android/tools/check-android-javadoc.pl [--root ANDROID_DIRECTORY]

The script uses F<android/> when F<android/app/src/main> exists and the
current directory otherwise, so it runs either from the repository root or
from F<android/>. C<--root> selects an Android directory explicitly and is
used by the isolated checker regression fixtures.

=head1 DESCRIPTION

The manual Android job runs Gradle C<sourceJavadoc> with
C<-Xdoclint:all,-missing -Werror>. A single malformed comment aborts that
lane, and the failing comment is usually one written long before. This
checker reads the same F<app/src/main/java> tree and reports the doclint
classes that a text review can decide without compiling:

=over 4

=item *

block tags outside the standard doclet set. C<@implNote>, C<@implSpec>
and C<@apiNote> are JDK-internal tags and remain unknown unless the
Gradle task registers them with C<-tag>;

=item *

an inline tag such as C<@code> used at the start of a comment line,
which javadoc treats as an unknown block tag;

=item *

raw angle-bracket placeholders in prose. C<TORRENT-DETAIL <tunnel> <id>>
is parsed as an HTML element, so doclint reports C<unknown tag: tunnel>.
Use C<{@code ...}> (which escapes its body) or C<&lt;...&gt;>;

=item *

a documented parameter that the following declaration does not have,
which doclint reports as an unresolvable reference.

=back

Missing documentation is deliberately not a failure: the Gradle task
passes C<-missing> to keep coverage a tracked task rather than a gate.

=head1 OUTPUT AND EXIT STATUS

Each defect is printed on STDERR followed by a C<FAIL:> summary, and the
exit status is 1. A clean run prints one C<OK:> line on STDOUT and exits
0. An absent Java tree or an unreadable source file is reported with C<FAIL:>
on STDOUT or aborts through C<die> (typically status 255).

=head1 FILE FORMAT

Java comments are matched with regular expressions rather than a parser,
because only core modules are allowed. C<{@code}> and C<{@literal}>
bodies are removed before the HTML scan, matching the javadoc rule that
their content is rendered verbatim.

=head1 DEPENDENCIES

Core Perl only (strict, warnings, File::Find, File::Spec, Getopt::Long,
5.016). No Gradle, javadoc, APK or device is involved.

=head1 SECURITY AND PORTABILITY

Read-only and offline: no Gradle invocation, no javadoc process, no
network. Diagnostics name a file, a line and a tag or identifier only;
comment prose and user-supplied strings are never echoed.
