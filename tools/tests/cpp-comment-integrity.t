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

sub scan_comments {
    my ($source) = @_;
    my @errors;
    my $offset = 0;
    my $line   = 1;
    my $state  = 'code';
    my $block_start;
    my $raw_end;

    while ( $offset < length $source ) {
        my $pair = substr( $source, $offset, 2 );
        my $char = substr( $source, $offset, 1 );

        if ( $state eq 'code' ) {
            if ( $pair eq '//' ) {
                $state = 'line';
                $offset += 2;
                next;
            }
            if ( $pair eq '/*' ) {
                $state       = 'block';
                $block_start = $line;
                $offset += 2;
                next;
            }

            my $rest = substr( $source, $offset );
            if ( $rest =~ /\A(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(/ ) {
                my $delimiter = $1;
                my $token     = $&;
                $raw_end = ")${delimiter}\"";
                $offset += length $token;
                $state = 'raw';
                next;
            }
            if ( $char eq '"' || $char eq "'" ) {
                $state = $char eq '"' ? 'string' : 'character';
                $offset++;
                next;
            }
            $line++ if $char eq "\n";
            $offset++;
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
            my $newlines = () = $consumed =~ /\n/g;
            $line += $newlines;
            $offset = $end + length $raw_end;
            $state  = 'code';
            next;
        }

        if ( $state eq 'string' || $state eq 'character' ) {
            if ( $char eq '\\' ) {
                my $escaped = substr( $source, $offset + 1, 1 );
                $line++ if $escaped eq "\n";
                $offset += 2;
                next;
            }
            my $closing = $state eq 'string' ? '"' : "'";
            if ( $char eq $closing ) {
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
    qr/line 3: nested block-comment opener/,
    'nested documentation comments are rejected'
);

my @unterminated = scan_comments("int f;\n/* missing close\n");
like(
    $unterminated[0] // '',
    qr/line 2: unterminated block comment/,
    'unterminated block comments are rejected'
);

my @valid =
  scan_comments( "const char *a = \"/* string */\";\n"
      . "auto b = R\"tag(/* raw string */)tag\"; // /* line comment */\n"
      . "/* ordinary closed comment */ int c;\n" );
is( scalar @valid, 0, 'strings, raw strings and closed comments are accepted' );

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
            return unless /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z/i;
            push @source_files, $File::Find::name;
        },
    },
    $root
);

my @tree_errors;
for my $path ( sort @source_files ) {
    open my $fh, '<:raw', $path or die "open $path: $!\n";
    local $/;
    my $source = <$fh>;
    close $fh or die "close $path: $!\n";
    push @tree_errors, map { "$path: $_" } scan_comments($source);
}
is( scalar @tree_errors,
    0,
    'maintained C/C++ files contain no nested or unterminated block comments' );
diag($_) for @tree_errors;

done_testing();
