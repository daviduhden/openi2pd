#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check doxygen params.
#
# Compile-free guard for the Doxygen failure class that has recurred in
# the source-docs lane: a documented function that names some parameters
# with @param but leaves others undocumented. Doxygen's
# WARN_IF_DOC_ERROR reports those and WARN_AS_ERROR turns them fatal;
# WARN_NO_PARAMDOC is disabled, so a function with no @param at all is
# intentionally accepted.
#
# The scan is deliberately conservative: it reads the same input roots
# and file patterns as docs/Doxyfile.source, attaches a documentation
# block only when the declaration follows immediately (no preprocessor
# line, other declaration or brace in between), and reports the exact
# file, line and missing names. It does not replace Doxygen; it makes
# the common partial-documentation defect fail in the static lanes
# before the manual source-docs job runs.
#
# Contract: exit 0 when every partially documented function is complete,
# 1 when a parameter is missing, and 2 on an execution error. The
# diagnostic goes to stderr; the success summary goes to stdout.
#

use strict;
use warnings;
use File::Find ();
use File::Spec ();

my @roots = (
	'libi2pd', 'libi2pd_client', 'privsep', 'daemon', 'tui', 'tools',
	'android/app/src/main/cpp'
);
my @extensions = ( '.h', '.hpp', '.c', '.cpp' );

my $root;
for ( my $i = 0; $i < @ARGV; $i++ ) {
	if ( $ARGV[$i] eq '--root' ) {
		die "check-doxygen-params: --root needs a path\n" if $i + 1 >= @ARGV;
		$root = $ARGV[ ++$i ];
	}
	elsif ( $ARGV[$i] eq '--help' ) {
		print "usage: check-doxygen-params.pl [--root DIR]\n";
		exit 0;
	}
	else {
		print STDERR "check-doxygen-params: unknown argument '$ARGV[$i]'\n";
		exit 2;
	}
}
$root //= $ENV{PROJECT_ROOT} // File::Spec->curdir ();
$root = File::Spec->rel2abs ($root);
if ( !-d $root ) {
	print STDERR "check-doxygen-params: not a directory: $root\n";
	exit 2;
}

my @files;
for my $subdir (@roots) {
	my $start = File::Spec->catdir ($root, split m{/}, $subdir);
	next unless -d $start;
	File::Find::find ({
		no_chdir => 1,
		wanted => sub {
			return unless -f $_;
			return if $_ =~ m{[\\/]tools[\\/]tests[\\/]};
			my $file = $_;
			return unless grep { $file =~ /\Q$_\E$/ } @extensions;
			push @files, $file;
		},
	}, $start);
}
@files = sort @files;

my $param_re = qr/\@param\s*(?:\[[^\]]*\])?\s*([A-Za-z_]\w*)/;
my $failures = 0;

sub mask_literals
{
	my ($text) = @_;
	my $out = '';
	my $i = 0;
	my $len = length $text;
	while ($i < $len) {
		my $ch = substr ($text, $i, 1);
		if ($ch eq '"' || $ch eq "'") {
			my $quote = $ch;
			$out .= ' ';
			$i++;
			while ($i < $len) {
				my $c = substr ($text, $i, 1);
				if ($c eq '\\') {
					$out .= ' ' if $i + 1 < $len;
					$out .= ' ' if $i < $len;
					$i += 2;
					next;
				}
				$out .= ' ';
				$i++;
				last if $c eq $quote;
			}
			next;
		}
		if ($ch eq '/' && substr ($text, $i, 2) eq '//') {
			my $end = index ($text, "\n", $i);
			$end = $len if $end < 0;
			$out .= ' ' x ($end - $i);
			$i = $end;
			next;
		}
		$out .= $ch;
		$i++;
	}
	return $out;
}

sub split_params
{
	my ($text) = @_;
	my @parts;
	my $depth = 0;
	my $current = '';
	for my $ch (split //, $text) {
		$depth++ if $ch =~ /[\(\[\{<]/;
		$depth-- if $ch =~ /[\)\]\}>]/;
		if ($ch eq ',' && $depth == 0) {
			push @parts, $current;
			$current = '';
			next;
		}
		$current .= $ch;
	}
	push @parts, $current if $current =~ /\S/;
	return @parts;
}

sub param_name
{
	my ($param) = @_;
	$param =~ s/^\s+|\s+$//g;
	return undef if $param eq '' || $param eq 'void' || $param eq '...';

	my $depth = 0;
	my $cut = -1;
	for my $i (0 .. length ($param) - 1) {
		my $ch = substr ($param, $i, 1);
		$depth++ if $ch =~ /[\(\[\{<]/;
		$depth-- if $ch =~ /[\)\]\}>]/;
		if ($ch eq '=' && $depth == 0) { $cut = $i; last; }
	}
	$param = substr ($param, 0, $cut) if $cut >= 0;
	$param =~ s/^\s+|\s+$//g;

	if ($param =~ /\(\s*\*\s*([A-Za-z_]\w*)\s*\)/) { return $1; }
	return undef unless $param =~ /([A-Za-z_]\w*)\s*$/;
	my $name = $1;
	return undef if $name =~ /^(?:const|volatile|unsigned|signed|struct|class|enum|typename)$/;
	return $name;
}

sub declaration_params
{
	my ($source, $masked, $position) = @_;
	my $tail = substr ($masked, $position, 800);
	$tail =~ s/^\s+//;
	my $open = index ($tail, '(');
	return undef if $open < 0;
	my $prefix = substr ($tail, 0, $open);
	return undef if $prefix =~ /[#;\{\}]/;
	return undef if ($prefix =~ tr/\n//) > 8;

	my $depth = 0;
	my $close = -1;
	for my $i ($open .. length ($tail) - 1) {
		my $ch = substr ($tail, $i, 1);
		$depth++ if $ch eq '(';
		if ($ch eq ')') {
			$depth--;
			if ($depth == 0) { $close = $i; last; }
		}
	}
	return undef if $close < 0;

	my $name_part = $prefix;
	$name_part =~ s/\b(?:template\s*<[^;{}]*?>|inline|static|constexpr|consteval|virtual|explicit|friend|extern|JNIEXPORT|JNICALL|__cdecl|CALLBACK|noexcept|const|override|final)\b/ /g;
	$name_part =~ s/^\s+|\s+$//g;
	return undef if $name_part eq '';
	return undef unless $name_part =~ /(?:[A-Za-z_]\w*|operator\s*[^\s(]+|~[A-Za-z_]\w*|\))\s*$/;
	my @words = split /\s+/, $name_part;
	my $last = $words[-1] // '';
	return undef if $last =~ /^(?:if|for|while|switch|catch|return|sizeof|decltype|static_assert|alignof)$/;

	my @names;
	for my $part (split_params (substr ($tail, $open + 1, $close - $open - 1))) {
		my $name = param_name ($part);
		push @names, $name if defined $name;
	}
	return \@names;
}

sub check_block
{
	my ($path, $source, $masked, $start, $end, $block) = @_;
	my %documented;
	while ($block =~ /$param_re/g) { $documented{$1} = 1; }
	return unless %documented; # WARN_NO_PARAMDOC is disabled.

	my $params = declaration_params ($source, $masked, $end);
	return unless defined $params;

	my @missing = grep { !$documented{$_} } @$params;
	return unless @missing;
	my $line = 1 + (substr ($source, 0, $start) =~ tr/\n//);
	(my $display = File::Spec->abs2rel ($path, $root)) =~ s{\\}{/}g;
	print STDERR "$display:$line: undocumented parameter(s): ", join (', ', @missing), "\n";
	$failures++;
}

for my $path (@files) {
	open my $fh, '<:raw', $path or do {
		print STDERR "check-doxygen-params: cannot read $path: $!\n";
		exit 2;
	};
	local $/;
	my $source = <$fh>;
	close $fh;
	$source = '' unless defined $source;

	my $masked = $source;
	$masked =~ s{/\*.*?\*/}{ ' ' x length ($&) }gse;
	$masked = mask_literals ($masked);

	while ($source =~ m{/\*\*(.*?)\*/}gs) {
		check_block ($path, $source, $masked, $-[0], $+[0], $1);
	}
	while ($source =~ m{((?:^[ \t]*///[^\n]*\n)+)}gm) {
		check_block ($path, $source, $masked, $-[0], $+[0], $1);
	}
}

if ($failures) {
	print STDERR "check-doxygen-params: $failures partially documented function(s)\n";
	exit 1;
}
print "check-doxygen-params: all partially documented functions name every parameter\n";
exit 0;
