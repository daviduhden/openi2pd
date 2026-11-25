#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: C/C++ comment token integrity.
#
# Catch nested and unterminated block comments before a platform compiler
# reaches the affected translation unit. This scanner is compile-free.
#

use strict;
use warnings;
use 5.016;

use File::Basename qw(basename);
use File::Find     qw(find);
use File::Spec;
use FindBin qw($Bin);
use Test::More;

sub code_token {
    my ( $source, $offset ) = @_;
    my $pair = substr( $source, $offset, 2 );
    return ( 'line',  2, undef ) if $pair eq '//';
    return ( 'block', 2, undef ) if $pair eq '/*';
    my ( $token, $delimiter ) =
      substr( $source, $offset ) =~ /\A((?:u8|u|U|L)?R"([^\s()\\]{0,16})\()/x;
    return ( 'raw', length($token), ")${delimiter}\"" ) if defined $token;

    my $char = substr( $source, $offset, 1 );
    return ( 'string',    1, undef ) if $char eq '"';
    return ( 'character', 1, undef ) if $char eq "'";
    return ( 'code',      1, undef );
}

sub scan_comments {
    my ($source) = @_;
    my @errors;
    my $offset = 0;
    my $line   = 1;
    my $state  = 'code';
    my $block_start;
    my $raw_end;
    my %closing = ( string => '"', character => "'" );

    while ( $offset < length $source ) {
        my $pair = substr( $source, $offset, 2 );
        my $char = substr( $source, $offset, 1 );

        if ( $state eq 'code' ) {
            my $width;
            ( $state, $width, $raw_end ) = code_token( $source, $offset );
            $block_start = $line if $state eq 'block';
            $line++ if $char eq "\n";
            $offset += $width;
            next;
        }

        if ( $state eq 'line' ) {
            if ( $char eq "\n" ) {
                $line++;
                $state = 'code';
            }
            $offset++;
            next;
        }

        if ( $state eq 'block' ) {
            if ( $pair eq '/*' ) {
                push @errors, "line $line: nested block-comment opener";

                # C and C++ do not nest comments; keep scanning the same
                # block so its first closing token remains authoritative.
                $offset += 2;
                next;
            }
            if ( $pair eq '*/' ) {
                $state = 'code';
                $offset += 2;
                next;
            }
            $line++ if $char eq "\n";
            $offset++;
            next;
        }

        if ( $state eq 'raw' ) {
            my $end = index( $source, $raw_end, $offset );
            if ( $end < 0 ) {
                push @errors, "line $line: unterminated raw string";
                last;
            }
            my $consumed =
              substr( $source, $offset, $end + length($raw_end) - $offset );
            my $newlines = () = $consumed =~ / \n /gx;
            $line += $newlines;
            $offset = $end + length $raw_end;
            $state  = 'code';
            next;
        }

        if ( exists $closing{$state} ) {
            if ( $char eq '\\' ) {
                my $escaped = substr( $source, $offset + 1, 1 );
                $line++ if $escaped eq "\n";
                $offset += 2;
                next;
            }
            if ( $char eq $closing{$state} ) {
                $state = 'code';
            }
            $line++ if $char eq "\n";
            $offset++;
            next;
        }
    }

    push @errors, "line $block_start: unterminated block comment"
      if $state eq 'block';
    return @errors;
}

my @nested = scan_comments("int f;\n/**\n/**\n */\n");
like(
    $nested[0] // '',
    qr/line\ 3:\ nested\ block-comment\ opener/x,
    'nested documentation comments are rejected'
);

my @unterminated = scan_comments("int f;\n/* missing close\n");
like(
    $unterminated[0] // '',
    qr/line\ 2:\ unterminated\ block\ comment/x,
    'unterminated block comments are rejected'
);

my @valid =
  scan_comments( "const char *a = \"/* string */\";\n"
      . "auto b = R\"tag(/* raw string */)tag\"; // /* line comment */\n"
      . "/* ordinary closed comment */ int c;\n" );
is( scalar @valid, 0, 'strings, raw strings and closed comments are accepted' );

my @after_raw =
  scan_comments("auto s = u8R\"tag(first\n/* raw text */)tag\";\n/*\n/*\n*/\n");
is_deeply(
    \@after_raw,
    ['line 4: nested block-comment opener'],
    'a multiline prefixed raw string preserves the next diagnostic line'
);

my $root = File::Spec->rel2abs(
    File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir ) );
my %skip = map { $_ => 1 }
  qw(.git .gradle .cache third_party vendor external node_modules out target dist build-output);
my @source_files;
find(
    {
        no_chdir => 1,
        wanted   => sub {
            if ( -d $_ ) {
                if ( $skip{ basename($_) } ) {
                    $File::Find::prune = 1;
                }
                return;
            }
            return unless / \. (?:c|cc|cpp|cxx|h|hh|hpp|hxx) \z /ix;
            push @source_files, $File::Find::name;
        },
    },
    $root
);

my @tree_errors;
for my $path ( sort @source_files ) {
    open my $fh, '<:raw', $path or die "open $path: $!\n";
    local $/ = undef;
    my $source = <$fh>;
    close $fh or die "close $path: $!\n";
    push @tree_errors, map { "$path: $_" } scan_comments($source);
}
is( scalar @tree_errors,
    0,
    'maintained C/C++ files contain no nested or unterminated block comments' );
diag($_) for @tree_errors;

done_testing();
