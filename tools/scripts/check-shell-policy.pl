#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: source-only interpreter embedding policy.
#
# Inspect host source and shell/configuration Markdown fences without
# executing a project program. Perl programs must be named files, never
# command strings or standard input; file-based syntax checks remain
# valid. This is a conservative command-shape scanner, not a complete
# parser for every host language or dynamic command construction.

use 5.016;
use strict;
use warnings;
use Cwd            qw(abs_path);
use File::Basename qw(basename);
use File::Find     ();
use File::Spec;
use FindBin      qw($Bin);
use Getopt::Long qw(GetOptionsFromArray);

my $root = File::Spec->rel2abs( File::Spec->catdir( $Bin, '..', '..' ) );
my $help;
Getopt::Long::Configure(qw(no_auto_abbrev no_ignore_case));
if ( !GetOptionsFromArray( \@ARGV, 'root=s' => \$root, 'help' => \$help )
    || @ARGV )
{
    print STDERR "shell-policy: invalid arguments; use --help\n";
    exit 2;
}
if ($help) {
    print "Usage: check-shell-policy.pl [--root DIRECTORY]\n";
    print
      "Read host sources and shell/configuration documentation fences only.\n";
    print
      "Exit 0: clean; 1: policy violation; 2: input or inspection failure.\n";
    exit 0;
}
$root = abs_path($root);
if ( !defined($root) || !-d $root ) {
    print STDERR "shell-policy: root is not a readable directory\n";
    exit 2;
}
my %skip = map { $_ => 1 }
  qw(.git .cache .gradle .idea node_modules build-output dist out target);
my %command_prefix = map { $_ => 1 } (
    "\n", qw(; | & COMMAND run:
      command: entrypoint: commandLine RUN exec command sudo nohup call)
);

sub discover_sources {
    my @paths;
    my $source_extensions = join '|', qw(sh bash ksh zsh fish cmd bat ps1
      yaml yml cmake mk mak gradle conf cfg ini toml properties json spec md);
    my $special_names  = join '|', qw(Makefile rules Doxyfile);
    my $container_file = qr{ (?:Dockerfile|Containerfile)[^/]* }x;
    my $discovered     = eval {
        local $SIG{__WARN__} = sub { die "source discovery failed\n"; };
        File::Find::find(
            {
                no_chdir   => 1,
                preprocess => sub {
                    return grep {
                        !$skip{$_}
                          && !( / ^build- /x
                            && -d File::Spec->catfile( $File::Find::dir, $_ ) )
                    } @_;
                },
                wanted => sub {
                    return if -l $File::Find::name || !-f $File::Find::name;
                    my $path = $File::Find::name;
                    my $name = basename($path);
                    return
                      if $name =~
                      / \.(?:pl|pm|t|py|pyw|cpp|cc|c|h|hpp|java)\z /ix;
                    if (
                           $name =~ / \.(?: $source_extensions )\z /ix
                        || $name =~ / \A(?: $special_names | CMakeLists\.txt
                            | \.perlcriticrc | $container_file )\z /x
                      )
                    {
                        push @paths, $path;
                        return;
                    }
                    open my $prefix, '<', $path
                      or die "cannot inspect source prefix\n";
                    my $line = '';
                    defined( read( $prefix, $line, 512 ) )
                      or die "cannot read source prefix\n";
                    close $prefix or die "cannot close source prefix\n";
                    push @paths, $path
                      if $line =~
                      / \A\#![^\n]*\b(?:sh|bash|ksh|zsh|fish|make)\b /x;
                },
            },
            $root
        );
        1;
    };
    unless ($discovered) {
        print STDERR "shell-policy: source discovery failed\n";
        exit 2;
    }
    return @paths;
}

my @violations;

sub inspect_source {
    my ($path) = @_;
    my $text;
    eval {
        die "source limit\n" if -s $path > 16 * 1024 * 1024;
        open my $file, '<:raw', $path or die "source read\n";
        local $/ = undef;
        $text = <$file> // '';
        close $file or die "source close\n";
        1;
    } or do {
        print STDERR "shell-policy: cannot inspect a source file\n";
        exit 2;
    };
    if ( $path =~ / \.md\z /ix ) {
        $text = documentation_commands($text);
    }
    $text = workflow_package_data($text) if $path =~ / \.ya?ml\z /ix;
    my $relative = join '/',
      File::Spec->splitdir( File::Spec->abs2rel( $path, $root ) );

    # A shebang supplies the source filename implicitly. Evaluate only
    # its explicit options, without confusing the following file body
    # with standard-input interpreter source.
    scan( "$1 source-file", $relative, 0, 0, 1 ) if $text =~ / \A\#!([^\n]*) /x;
    my $cmake = $path =~ / (?:\.cmake|CMakeLists\.txt)\z /ix ? 1 : 0;
    scan( $text, $relative, $cmake, 0, 1 );
    return;
}

my @files = discover_sources();
inspect_source($_) for sort @files;
for my $issue (@violations) {

    # Never print source operands: configuration can contain credentials.
    print STDERR
      "shell-policy: $issue->{path}:$issue->{line}: $issue->{reason}\n";
}
print 'shell-policy: '
  . scalar(@files)
  . " source file(s), "
  . scalar(@violations)
  . " policy violation(s)\n";
exit( @violations ? 1 : 0 );

# setup-msys2's install/pacboy block scalars are package names, not
# commands. Mask only literal package lists in that action's with map;
# retain every newline and scan malformed or executable-looking input.
# Other actions, run blocks and generic YAML remain conservative inputs.
sub literal_package_list {
    my ($line) = @_;
    my $package = qr{ [A-Za-z0-9][A-Za-z0-9_.+\@:-]* }x;
    return $line =~ / \A\ *$package(?:\ +$package)*\ *(?:\#.*)?\z /x;
}

sub is_msys_setup_action {
    my ($line) = @_;
    my $action = qr{ msys2/setup-msys2\@[A-Za-z0-9_./-]+ }x;
    return $line =~ m{ \A\ *(?:-\ +)?uses:\ *['"]?$action['"]?\ *(?:\#.*)?\z }x;
}

sub is_package_scalar {
    my ( $line, $indent, $child_indent ) = @_;
    return
         defined($child_indent)
      && $indent == $child_indent
      && $line =~ / \A\ *(?:install|pacboy):\ *[>|][+-]?\ *(?:\#.*)?\z /x;
}

sub workflow_package_data {
    my ($text) = @_;
    my @lines  = split / \n /x, $text, -1;
    my (
        $step_indent,  $key_indent, $with_indent,
        $child_indent, $package_indent
    );
    my $msys = 0;
    for my $line (@lines) {
        next if $line =~ / \A\s*(?:\#.*)?\z /x;
        my ($padding) = $line =~ / \A(\ *) /x;
        my $indent    = length $padding;
        if ( defined($package_indent) && $indent > $package_indent ) {
            $line = '' if literal_package_list($line);
            next;
        }
        undef $package_indent;
        if ( defined($step_indent) && $indent <= $step_indent ) {
            undef $step_indent;
            undef $key_indent;
            undef $with_indent;
            undef $child_indent;
            $msys = 0;
        }
        if ( $line =~ / \A(\ *)-(\ +)\S /x ) {
            $step_indent = length $1;
            $key_indent  = length($1) + 1 + length($2);
            undef $with_indent;
            undef $child_indent;
            $msys = 0;
        }
        next unless defined $step_indent;
        if ( is_msys_setup_action($line) ) {
            $msys = 1;
            next;
        }
        next unless $msys;
        if ( defined($with_indent) && $indent <= $with_indent ) {
            undef $with_indent;
            undef $child_indent;
        }
        if ( $line =~ / \A\ *with:\ *(?:\#.*)?\z /x && $indent == $key_indent )
        {
            $with_indent = $indent;
            next;
        }
        $child_indent //= $indent
          if defined($with_indent) && $indent > $with_indent;
        $package_indent = $indent
          if is_package_scalar( $line, $indent, $child_indent );
    }
    return join "\n", @lines;
}

sub documentation_commands {
    my ($text) = @_;
    my ( $fence, $enabled ) = ( '', 0 );
    my %command_languages = map { $_ => 1 } qw(sh shell shell-session bash
      ksh zsh fish console powershell ps1 cmd bat make makefile cmake yaml yml);
    my @kept;
    for my $line ( split / \n /x, $text, -1 ) {
        if (  !$fence
            && $line =~ / ^\ {0,3}(`{3,}|~{3,})\s*([A-Za-z0-9_-]*)\s*$ /x )
        {
            my ( $marker, $language ) = ( $1, lc $2 );
            $fence   = $marker;
            $enabled = exists $command_languages{$language};
            push @kept, '';
        }
        elsif ($fence
            && $line =~ / ^\ {0,3}(`{3,}|~{3,})\s*$ /x
            && substr( $1, 0, 1 ) eq substr( $fence, 0, 1 )
            && length($1) >= length($fence) )
        {
            ( $fence, $enabled ) = ( '', 0 );
            push @kept, '';
        }
        else {
            push @kept, $enabled ? $line : '';
        }
    }
    return join "\n", @kept;
}

# Preserve newline tokens and line numbers. Quotes group operands; only
# escaped quotes and command continuations affect this lexical pass.
sub interpreter_variable {
    my ($source) = @_;
    my $variable_name = qr{ (?:PERL|PYTHON)(?:_EXECUTABLE)?
        | Python[0-9]*_EXECUTABLE }x;
    my ($variable) = $source =~ / \A(\$\($variable_name\)) /x;
    return $variable;
}

sub quoted_character {
    my ( $quote, $character, $next ) = @_;
    return ( $next, $quote, 1, 0 ) if $character eq '\\' && $next eq $quote;
    return ( '',    '',     0, 0 ) if $character eq $quote;
    return ( $character, $quote, 0, $character eq "\n" ? 1 : 0 );
}

sub is_line_continuation {
    my ( $character, $next ) = @_;
    return $character =~ / [\\`] /x && $next eq "\n";
}

sub tokens {
    my ( $text, $first_line ) = @_;
    my @tokens;
    my ( $word, $quote, $start ) = ( '', '', $first_line );
    my $line = $first_line;
    for ( my $at = 0 ; $at < length $text ; ++$at ) {
        my $char = substr( $text, $at,     1 );
        my $next = substr( $text, $at + 1, 1 );
        my $variable =
          !$quote && $char eq '$'
          ? interpreter_variable( substr( $text, $at ) )
          : undef;
        if ( defined $variable ) {
            $start = $line if $word eq '';
            $word .= $variable;
            $at += length($variable) - 1;
            next;
        }
        if ( is_line_continuation( $char, $next ) ) {
            ++$at;
            ++$line;
            next;
        }
        if ($quote) {
            my ( $literal, $remaining_quote, $skip, $line_break ) =
              quoted_character( $quote, $char, $next );
            $word .= $literal;
            $quote = $remaining_quote;
            $at   += $skip;
            $line += $line_break;
            next;
        }
        if ( $char =~ / ["'] /x ) {
            $start = $line if $word eq '';
            $quote = $char;
        }
        elsif ( $char eq '#' && $word eq '' ) {
            my $end = index( $text, "\n", $at );
            $at = ( $end < 0 ? length($text) : $end ) - 1;
        }
        elsif ( $char =~ / [\s;|&<>()\[\],] /x ) {
            push @tokens, { text => $word, line => $start } if length $word;
            $word = '';
            push @tokens, { text => $char, line => $line }
              if $char !~ / [ \t\r] /x;
            ++$line if $char eq "\n";
        }
        else {
            $start = $line if $word eq '';
            $word .= $char;
        }
    }
    push @tokens, { text => $word, line => $start } if length $word;
    return @tokens;
}

sub interpreter {
    my ($text) = @_;
    $text =~ s/ \A\@(?=(?:.*[\\\/])?(?:perl|python|pypy)) //ix;
    my $perl_variable = qr{ PERL(?:_EXECUTABLE)? }x;
    my $dollars       = qr{ \$\$? }x;
    my $version       = qr{ [0-9]+(?:\.[0-9]+)* }x;
    return 'perl'
      if $text =~ / \A(?:.*[\\\/])?perl$version?(?:\.exe)?\z /ix
      || $text =~ / \A(?: $dollars\{?$perl_variable\}?
          | $dollars\($perl_variable\) | %$perl_variable% )\z /x;
    return 'python'
      if $text =~ / \A(?:.*[\\\/])?(?:python|pypy)$version?(?:\.exe)?\z /ix
      || $text =~ / \A\$\{?Python[0-9]*_EXECUTABLE\}?\z /x;
    return;
}

sub command_position {
    my ( $tokens, $at ) = @_;
    return 1 if $at == 0;
    my $before = $tokens->[ $at - 1 ]{text};
    return 1
      if $before eq '$' && ( $at < 2 || $tokens->[ $at - 2 ]{text} eq "\n" );
    return 1 if exists $command_prefix{$before};
    return 1
      if $before eq '['
      && $at >= 2
      && $tokens->[ $at - 2 ]{text} =~
      / \A(?:COMMAND|commandLine|command:|entrypoint:|RUN)\z /x;
    return 1
      if $before eq 'shell'
      && $at >= 3
      && $tokens->[ $at - 2 ]{text} eq '('
      && $tokens->[ $at - 3 ]{text} eq '$';
    return 1
      if $before eq '('
      && ( $at < 2 || $tokens->[ $at - 2 ]{text} =~ / \A(?:\$|\n|;|\||&)\z /x );
    return 1
      if $before eq '-' && ( $at < 2 || $tokens->[ $at - 2 ]{text} eq "\n" );

    return follows_wrapper( $tokens, $at );
}

sub follows_wrapper {
    my ( $tokens, $at ) = @_;

    # env may precede assignments, options or an absolute executable.
    my $wrapper = qr{ env | command | sudo | nohup | exec | cmd(?:\.exe)? }ix;
    for ( my $back = $at - 1 ; $back >= 0 ; --$back ) {
        my $part = $tokens->[$back]{text};
        return 1
          if $part =~ / \A(?:.*[\\\/])? $wrapper \z /ix;
        last
          unless $part =~
          / \A(?:[-\/][A-Za-z-]+|[A-Za-z_][A-Za-z0-9_]*=.*)\z /x;
    }
    return 0;
}

sub scan {
    my ( $text, $path, $cmake, $depth, $first_line ) = @_;
    return if $depth > 4;
    my @tokens = tokens( $text, $first_line );
    for ( my $at = 0 ; $at < @tokens ; ++$at ) {
        my $token = $tokens[$at];
        next unless command_position( \@tokens, $at );
        if ( $token->{text} =~ / \A--split-string=(.*) /sx ) {
            scan( $1, $path, $cmake, $depth + 1, $token->{line} );
            next;
        }
        if (   $token->{text} =~ / \s /x
            && $token->{text} =~
            / \A(?:[^\s]*[\\\/])?(?:perl|python[0-9.]*|pypy[0-9.]*|env)\s /x )
        {
            scan( $token->{text}, $path, $cmake, $depth + 1, $token->{line} );
            next;
        }
        my $language = interpreter( $token->{text} );
        next unless $language;
        my $reason =
          interpreter_source_issue( \@tokens, $at, $cmake, $language );
        push @violations,
          { path => $path, line => $token->{line}, reason => $reason }
          if $reason;
    }
    return;
}

sub interpreter_source_issue {
    my ( $tokens, $at,    $cmake,  $language )     = @_;
    my ( $file,   $query, $reason, $options_done ) = ( 0, 0, '', 0 );
    for ( my $arg = $at + 1 ; $arg < @{$tokens} ; ++$arg ) {
        my $value = $tokens->[$arg]{text};
        next if $value eq ',';
        next if $value eq "\n" && $cmake;
        last if $value =~ / \A(?:\n|;|\||&|\)|>)\z /x;
        if ( $value eq '<' || $value eq '-' ) {
            $reason = 'interpreter source from standard input is forbidden';
            last;
        }
        if ($options_done)    { $file         = 1; last; }
        if ( $value eq '--' ) { $options_done = 1; next; }
        if ( $value =~ / \A(?:--version|--help|-[vh]|-V(?::.*)?)\z /x ) {
            $query = 1;
            next;
        }
        my ( $inline, $operand ) = option_source( $language, $value );
        if ($inline) {
            $reason = 'inline interpreter source is forbidden';
            last;
        }
        if ($operand) {
            ++$arg;
            if (
                $arg < @{$tokens}
                && is_inline_perl_module(
                    $language, $operand, $tokens->[$arg]{text}
                )
              )
            {
                $reason = 'inline interpreter source is forbidden';
                last;
            }
            next;
        }
        next if $value =~ / \A- /x;
        $file = 1;
        last;
    }
    $reason = 'interpreter source from standard input is forbidden'
      if !$reason && !$file && !$query;
    return $reason;
}

# Short switches may be bundled. Operand-taking switches consume the
# remaining token (or the next one), so the E in -MEncode is module
# data, while -npe and -we still expose an inline-program switch.
sub option_source {
    my ( $language, $option ) = @_;
    return ( 1, 0 ) if $option     =~ / \A--(?:eval|execute)(?:=|\z) /x;
    return ( 0, 0 ) unless $option =~ / \A-([^-].*)\z /x;
    my $flags         = $1;
    my $operand_flags = $language eq 'perl' ? 'IMmFD' : 'mWX';
    for ( my $at = 0 ; $at < length $flags ; ++$at ) {
        my $flag = substr( $flags, $at, 1 );
        if ( $language eq 'perl' ) {
            return ( 1, 0 ) if $flag =~ / [eEx] /x;
            return ( 0, 0 ) if $flag eq 'i';
            return ( 0, 0 )
              if $flag eq 'd' && substr( $flags, $at + 1, 1 ) eq ':';
        }
        elsif ( $language eq 'python' && $flag eq 'c' ) {
            return ( 1, 0 );
        }
        next if index( $operand_flags, $flag ) < 0;
        my $payload = substr( $flags, $at + 1 );
        return ( 1, 0 ) if is_inline_perl_module( $language, $flag, $payload );
        return ( 0, $payload eq '' ? $flag : 0 );
    }
    return ( 0, 0 );
}

sub is_inline_perl_module {
    my ( $language, $flag, $payload ) = @_;
    return $language eq 'perl' && $flag =~ / [Mm] /x && $payload =~ / [;{}] /x;
}
