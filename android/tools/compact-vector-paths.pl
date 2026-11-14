#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# Preserve the canonical symbol geometry while shortening Android paths.
# Decimal coordinates use exact integer hundredths. No float rounding.

use strict;
use warnings;
use FindBin;
use File::Spec;
use File::Temp   qw(tempfile);
use Getopt::Long qw(GetOptions);

my $root  = File::Spec->rel2abs("$FindBin::Bin/../..");
my $write = 0;
GetOptions( 'root=s' => \$root, 'write' => \$write )
  or die "usage: compact-vector-paths.pl [--root DIR] [--write]\n";
@ARGV == 0 or die "unexpected positional operand\n";

sub read_text {
    my ($path) = @_;
    open my $fh, '<:raw', $path or die "cannot read vector source\n";
    local $/;
    my $text = <$fh>;
    close $fh or die "cannot close vector source\n";
    return $text;
}

sub hundredths {
    my ($number) = @_;
    $number =~ /\A(-?)(\d+)(?:\.(\d{1,2}))?\z/
      or die "vector precision or numeric syntax needs explicit review\n";
    my ( $sign, $whole, $fraction ) = ( $1, $2, $3 // '' );
    length($whole) <= 7 or die "vector coordinate exceeds producer bound\n";
    $fraction .= '0' while length($fraction) < 2;
    my $value = 100 * $whole + $fraction;
    return $sign eq '-' ? -$value : $value;
}

sub decimal {
    my ($value) = @_;
    my $sign = $value < 0 ? '-' : '';
    $value = abs $value;
    my $fraction = sprintf '%02d', $value % 100;
    $fraction =~ s/0+\z//;
    return
        $sign
      . int( $value / 100 )
      . ( length($fraction) ? ".$fraction" : '' );
}

# Expand explicit or repeated M/L/C/Z commands to exact absolute points.
# Retain contour order and close-path boundaries, so evenOdd holes
# cannot be accidentally split or filled by regeneration.
sub absolute_commands {
    my ($path) = @_;
    my @tokens;
    pos($path) = 0;
    while ( pos($path) < length($path) ) {
        if ( $path =~ /\G\s*([MLCZmlcz]|-?\d+(?:\.\d+)?)\s*/gc ) {
            push @tokens, $1;
        }
        else {
            die "unsupported vector command or separator\n";
        }
    }
    my ( $command, $x, $y, $start_x, $start_y ) = ( '', 0, 0, 0, 0 );
    my @commands;
    while (@tokens) {
        $command = shift @tokens if $tokens[0] =~ /\A[A-Za-z]\z/;
        length($command) or die "vector operand lacks a command\n";
        my $kind = uc $command;
        if ( $kind eq 'Z' ) {
            push @commands, ['Z'];
            ( $x, $y ) = ( $start_x, $start_y );
            $command = '';
            next;
        }
        my $count = $kind eq 'C' ? 6 : 2;
        @tokens >= $count or die "truncated vector command\n";
        my @points = map { hundredths($_) } splice @tokens, 0, $count;
        if ( $command eq lc $command ) {
            for my $index ( 0 .. $#points ) {
                $points[$index] += $index % 2 ? $y : $x;
            }
        }
        push @commands, [ $kind, @points ];
        ( $x, $y ) = @points[ -2, -1 ];
        if ( $kind eq 'M' ) {
            ( $start_x, $start_y ) = ( $x, $y );
            $command = $command eq 'm' ? 'l' : 'L';
        }
    }
    return \@commands;
}

sub compact {
    my ($commands) = @_;
    my ( $x, $y, $start_x, $start_y, $last ) = ( 0, 0, 0, 0, '' );
    my $result = '';
    for my $item ( @{$commands} ) {
        my ( $kind, @points ) = @{$item};
        my $command  = $kind eq 'M' ? 'M' : lc $kind;
        my @operands = @points;
        if ( $kind eq 'L' || $kind eq 'C' ) {
            for my $index ( 0 .. $#operands ) {
                $operands[$index] -= $index % 2 ? $y : $x;
            }
        }
        $result .=
          $command eq $last && $kind ne 'M' && $kind ne 'Z' ? ' ' : $command;
        $result .= join ' ', map { decimal($_) } @operands;
        if ( $kind eq 'Z' ) {
            ( $x, $y ) = ( $start_x, $start_y );
        }
        else {
            ( $x,       $y )       = @points[ -2, -1 ];
            ( $start_x, $start_y ) = ( $x, $y ) if $kind eq 'M';
        }
        $last = $command;
    }
    return $result;
}

sub command_key {
    my ($commands) = @_;
    return join ';', map { join ',', @{$_} } @{$commands};
}

my $master =
  read_text( File::Spec->catfile( $root, 'assets', 'branding', 'symbol.svg' ) );
my @master_paths = $master =~ /<path\b[^>]*\bd="([^"]+)"/g;
@master_paths == 4 or die "canonical symbol path inventory changed\n";
my @commands      = map { absolute_commands($_) } @master_paths;
my @compact_paths = map { compact($_) } @commands;
for my $index ( 0 .. $#compact_paths ) {
    length( $compact_paths[$index] ) < 3000
      or die "lossless path still exceeds reviewed Lint bound\n";
    command_key( absolute_commands( $compact_paths[$index] ) ) eq
      command_key( $commands[$index] )
      or die "producer changed canonical vector geometry\n";
}

my @outputs;
for my $name (
    qw(ic_logo ic_logo_monochrome ic_launcher_foreground ic_launcher_monochrome)
  )
{
    my $path = File::Spec->catfile( $root, 'android', 'app', 'src', 'main',
        'res', 'drawable', "$name.xml" );
    my $text     = read_text($path);
    my @paths    = $text =~ /android:pathData="([^"]+)"/g;
    my $expected = $name =~ /monochrome\z/ ? 3 : 4;
    @paths == $expected or die "Android symbol path inventory changed\n";
    for my $index ( 0 .. $#paths ) {
        command_key( absolute_commands( $paths[$index] ) ) eq
          command_key( $commands[$index] )
          or die "Android vector differs from canonical symbol\n";
    }
    my $index   = 0;
    my $updated = $text;
    $updated =~ s/android:pathData="[^"]+"/
        'android:pathData="' . $compact_paths[$index++] . '"'/ge;
    push @outputs, [ $path, $text, $updated ];
}

# Validate every template before any output is replaced.
for my $output (@outputs) {
    my ( $path, $text, $updated ) = @{$output};
    if ( $write && $updated ne $text ) {
        -l $path and die "refusing a symlink vector destination\n";
        my ( $fh, $temporary ) = tempfile(
            '.vector-XXXXXX',
            DIR => File::Spec->catdir(
                $root, 'android', 'app', 'src', 'main', 'res', 'drawable'
            ),
            UNLINK => 1
        );
        print {$fh} $updated or die "cannot write compact vector\n";
        close $fh            or die "cannot close compact vector\n";
        chmod( ( stat $path )[2] & 07777, $temporary )
          or die "cannot preserve vector mode\n";
        rename $temporary, $path or die "cannot replace compact vector\n";
    }
    elsif ( $updated ne $text ) {
        die "Android vector literals need --write regeneration\n";
    }
}
print "Android vector literals preserve canonical geometry below Lint bound\n";

__END__

=head1 NAME

compact-vector-paths.pl - preserve canonical Android vector geometry

=head1 SYNOPSIS

  perl android/tools/compact-vector-paths.pl
  perl android/tools/compact-vector-paths.pl --write

=head1 CONTRACT

The default mode reads the canonical SVG and the four Android templates.
It checks their exact decimal command streams, contour order and close-path
boundaries, then requires canonical compact literals below 3000 characters.
C<--write> updates only path literals in those templates after validating all
inputs. It never modifies the SVG master, groups, transforms, colors or fills.

Absolute cubic and line coordinates become relative coordinates using exact
integer hundredths; repeated command letters and redundant decimal zeroes are
omitted. Unsupported commands or precision require an explicit producer review.
This preserves mathematical geometry, including even-odd holes. It does not
prove pixel-identical floating-point rasterization or replace Android Lint and
rendering validation in manual CI.

C<--root DIR> selects an explicit repository root for isolated CI fixtures.
No dependency, compiler, renderer or Android runtime is invoked.

=cut
