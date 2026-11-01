#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check doxygen params.
#
# Compile-free guard for the parameter-documentation defects that recur
# in the source-docs lane:
#
#   1. a documented function that names some parameters with @param but
#      leaves others undocumented;
#   2. two @param entries for the same parameter inside one block;
#   3. a .c/.cpp definition that repeats the @param documentation its
#      header declaration already carries. Doxygen merges a member's
#      declaration and definition documentation, so the parameter is
#      documented twice and the run aborts.
#
# Doxygen reports these under WARN_IF_DOC_ERROR and WARN_AS_ERROR turns
# them fatal; WARN_NO_PARAMDOC is disabled, so a function with no @param
# at all is intentionally accepted.
#
# The scan is deliberately conservative: it reads the same input roots
# and file patterns as docs/Doxyfile.source, attaches a documentation
# block only when the declaration follows immediately (no preprocessor
# line, other declaration or brace in between), and reports the exact
# file, line and names. Rule 3 has to decide whether a definition and a
# declaration are the same entity; it compares the enclosing
# namespace/class chain, the unqualified function name and the parameter
# count, so an overload with a different arity or a same-named function
# in another scope is not reported. Two same-arity overloads in one
# scope cannot be told apart without a full C++ parser and stay a known
# limit.
#
# Contract: exit 0 when every block is complete and declared once, 1
# when a defect is found, and 2 on an execution error. The diagnostics
# go to stderr; the success summary goes to stdout.
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

# Describe the declaration that follows the block: the documented
# parameter names, the parameter count (unnamed parameters included,
# because they are still part of the signature) and the possibly
# qualified function name.
sub declaration_info
{
	my ($masked, $position) = @_;
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
	return undef unless $name_part =~ /((?:[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)|operator\s*[^\s(]+|~[A-Za-z_]\w*)\s*$/;
	my $full = $1;
	my @words = split /\s+/, $name_part;
	my $last = $words[-1] // '';
	return undef if $last =~ /^(?:if|for|while|switch|catch|return|sizeof|decltype|static_assert|alignof)$/;

	my @names;
	my $arity = 0;
	for my $part (split_params (substr ($tail, $open + 1, $close - $open - 1))) {
		(my $plain = $part) =~ s/^\s+|\s+$//g;
		next if $plain eq '' || $plain eq 'void' || $plain eq '...';
		$arity++;
		my $name = param_name ($part);
		push @names, $name if defined $name;
	}

	my ($qualifier, $simple) = ('', $full);
	if ($full =~ /^(.*)::([A-Za-z_]\w*)$/) { $qualifier = $1; $simple = $2; }
	return {
		names => \@names, arity => $arity, name => $simple,
		qualifier => $qualifier
	};
}

# Classify one opening brace by the statement text that precedes it. A
# namespace, class, struct or union contributes its name to the scope
# chain; every other brace is a marker, so a declaration inside a
# function body cannot be mistaken for a namespace-level declaration.
sub scope_marker
{
	my ($tail) = @_;
	$tail =~ s/^\s+|\s+$//g;
	$tail =~ s/^template\s*<[^;{}]*?>\s*//;
	return '{}' unless $tail =~ /^(namespace|class|struct|union)\b(.*)$/s;
	my $kind = $1;
	my $rest = $2;
	if ($kind eq 'namespace') {
		return $1 if $rest =~ /^\s*([A-Za-z_]\w*)/;
		return '{}';
	}
	$rest =~ s/:.*$//s;    # drop the base clause before the opening brace
	my @words = grep { /^[A-Za-z_]\w*$/ } split /\s+/, $rest;
	@words = grep { !/^(?:final|alignas)$/ } @words;
	@words = grep { !/^[A-Z][A-Z0-9_]*$/ } @words;    # export macro
	return $words[-1] if @words;
	return '{}';
}

sub scope_at
{
	my ($masked, $position) = @_;
	my @stack;
	my $tail = '';
	my $directive = 0;
	my $length = length $masked;
	my $limit = $position < $length ? $position : $length;
	for (my $i = 0; $i < $limit; ++$i) {
		my $ch = substr ($masked, $i, 1);
		if ($ch eq '{') {
			push @stack, scope_marker ($tail);
			$tail = '';
			next;
		}
		if ($ch eq '}') { pop @stack; $tail = ''; next; }
		if ($ch eq ';') { $tail = ''; next; }
		if ($ch eq '#') { $directive = 1; }
		if ($ch eq "\n") {
			# A preprocessor line is not part of a declaration head, so it
			# must not stop the next scope keyword from being recognised.
			$tail = '' if $directive;
			$directive = 0;
			next;
		}
		$tail .= $ch;
		$tail = substr ($tail, -400) if length ($tail) > 400;
	}
	return \@stack;
}

# Identity of the documented entity. The enclosing scope plus the
# qualifier written on the name covers "void Class::name (...)" in a
# .cpp against the in-class declaration in the header.
sub entity_key
{
	my ($masked, $start, $info) = @_;
	my @scope = @{ scope_at ($masked, $start) };
	my $prefix = join ('::', @scope);
	my $qualifier = $info->{qualifier};
	my $leading = $prefix . '::';
	if ($prefix ne '' && index ($qualifier, $leading) == 0) {
		$qualifier = substr ($qualifier, length ($leading));
	}
	push @scope, $qualifier if $qualifier ne '';
	return join ('::', @scope) . "\x00" . $info->{name} . "\x00"
		. $info->{arity};
}

sub display_path
{
	my ($path) = @_;
	my $display = File::Spec->abs2rel ($path, $root);
	$display =~ s{\\}{/}g;
	return $display;
}

sub doc_blocks
{
	my ($source) = @_;
	my @blocks;
	while ($source =~ m{/\*\*(.*?)\*/}gs) {
		push @blocks, { start => $-[0], end => $+[0], text => $1 };
	}
	while ($source =~ m{((?:^[ \t]*///[^\n]*\n)+)}gm) {
		push @blocks, { start => $-[0], end => $+[0], text => $1 };
	}
	return @blocks;
}

sub block_line
{
	my ($source, $start) = @_;
	return 1 + (substr ($source, 0, $start) =~ tr/\n//);
}

sub read_source
{
	my ($path) = @_;
	open my $fh, '<:raw', $path
		or do {
			print STDERR "check-doxygen-params: cannot read $path: $!\n";
			exit 2;
		};
	local $/;
	my $source = <$fh>;
	close $fh;
	return defined $source ? $source : '';
}

my @documents;
for my $path (@files) {
	my $source = read_source ($path);
	my $masked = $source;
	$masked =~ s{/\*.*?\*/}{ ' ' x length ($&) }gse;
	$masked = mask_literals ($masked);
	push @documents, {
		path => $path, source => $source, masked => $masked,
		is_header => ( $path =~ /\.(?:h|hpp)$/ ? 1 : 0 )
	};
}

# Pass 1: every header declaration that documents parameters. Rule 3
# keeps a definition from repeating that contract; the first declaration
# owns it.
my %header_contract;
for my $document (grep { $_->{is_header} } @documents) {
	for my $block (doc_blocks ($document->{source})) {
		my %documented;
		while ($block->{text} =~ /$param_re/g) { $documented{$1} = 1; }
		next unless %documented;
		my $info = declaration_info ($document->{masked}, $block->{end});
		next unless $info;
		my $key = entity_key ($document->{masked}, $block->{start}, $info);
		next if exists $header_contract{$key};
		$header_contract{$key} = {
			path => display_path ($document->{path}),
			line => block_line ($document->{source}, $block->{start})
		};
	}
}

# Pass 2: the per-block rules and the declaration/definition duplicate.
for my $document (@documents) {
	my $source = $document->{source};
	my $masked = $document->{masked};
	for my $block (doc_blocks ($source)) {
		my %documented;
		while ($block->{text} =~ /$param_re/g) { $documented{$1} = 1; }
		next unless %documented;    # WARN_NO_PARAMDOC is disabled.

		my $line = block_line ($source, $block->{start});
		my $display = display_path ($document->{path});

		my %seen;
		my @repeated;
		while ($block->{text} =~ /$param_re/g) {
			push @repeated, $1 if $seen{$1}++;
		}
		if (@repeated) {
			print STDERR "$display:$line: duplicate \@param '$_' in one "
				. "documentation block\n" for @repeated;
			$failures++;
		}

		my $info = declaration_info ($masked, $block->{end});
		next unless $info;

		my @missing = grep { !$documented{$_} } @{ $info->{names} };
		if (@missing) {
			print STDERR "$display:$line: undocumented parameter(s): ",
				join (', ', @missing), "\n";
			$failures++;
		}

		next if $document->{is_header};
		my $key = entity_key ($masked, $block->{start}, $info);
		my $declaration = $header_contract{$key} or next;
		print STDERR "$display:$line: duplicate \@param documentation for ",
			"$info->{name}; the declaration in $declaration->{path}:",
			"$declaration->{line} already documents it\n";
		$failures++;
	}
}

if ($failures) {
	print STDERR "check-doxygen-params: $failures parameter documentation "
		. "defect(s)\n";
	exit 1;
}
print "check-doxygen-params: parameter documentation is complete and "
	. "declared once\n";
exit 0;
