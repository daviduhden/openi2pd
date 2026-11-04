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
use Cwd qw(abs_path);
use File::Basename qw(basename);
use File::Find ();
use File::Spec;
use FindBin qw($Bin);
use Getopt::Long qw(GetOptionsFromArray);

my $root = File::Spec->rel2abs(File::Spec->catdir($Bin, '..', '..'));
my $help;
Getopt::Long::Configure(qw(no_auto_abbrev no_ignore_case));
if (!GetOptionsFromArray(\@ARGV, 'root=s' => \$root, 'help' => \$help) || @ARGV) {
    print STDERR "shell-policy: invalid arguments; use --help\n";
    exit 2;
}
if ($help) {
    print "Usage: check-shell-policy.pl [--root DIRECTORY]\n";
    print "Read host sources and shell/configuration documentation fences only.\n";
    print "Exit 0: clean; 1: policy violation; 2: input or inspection failure.\n";
    exit 0;
}
$root = abs_path($root);
if (!defined($root) || !-d $root) {
    print STDERR "shell-policy: root is not a readable directory\n";
    exit 2;
}
my %skip = map { $_ => 1 } qw(.git .cache .gradle .idea node_modules build-output dist out target);
my @files;
my $failure;
eval {
    local $SIG{__WARN__} = sub { die "source discovery failed\n"; };
    File::Find::find({
        no_chdir => 1,
        preprocess => sub {
            return grep { !$skip{$_}
                && !(/^build-/ && -d File::Spec->catfile($File::Find::dir, $_)) } @_;
        },
        wanted => sub {
            return if -l $File::Find::name || !-f $File::Find::name;
            my $path = $File::Find::name;
            my $name = basename($path);
            return if $name =~ /\.(?:pl|pm|t|py|pyw|cpp|cc|c|h|hpp|java)\z/i;
            if ($name =~ /\.(?:sh|bash|ksh|zsh|fish|cmd|bat|ps1|ya?ml|cmake|mk|mak|gradle|conf|cfg|ini|toml|properties|json|spec|md)\z/i
                || $name =~ /\A(?:Makefile|CMakeLists\.txt|Doxyfile|\.perlcriticrc|Dockerfile[^\/]*|Containerfile[^\/]*|rules)\z/) {
                push @files, $path;
                return;
            }
            open my $prefix, '<', $path or die "cannot inspect source prefix\n";
            my $line = '';
            defined(read($prefix, $line, 512)) or die "cannot read source prefix\n";
            close $prefix or die "cannot close source prefix\n";
            push @files, $path if $line =~ /\A#![^\n]*\b(?:sh|bash|ksh|zsh|fish|make)\b/;
        },
    }, $root);
    1;
} or $failure = $@ || 'source discovery failed';
if ($failure) {
    print STDERR "shell-policy: source discovery failed\n";
    exit 2;
}
my @violations;
for my $path (sort @files) {
    my $text;
    eval {
        die "source limit\n" if -s $path > 16 * 1024 * 1024;
        open my $file, '<:raw', $path or die "source read\n";
        local $/;
        $text = <$file> // '';
        close $file or die "source close\n";
        1;
    } or do {
        print STDERR "shell-policy: cannot inspect a source file\n";
        exit 2;
    };
    if ($path =~ /\.md\z/i) {
        $text = documentation_commands($text);
    }
    my $relative = join '/', File::Spec->splitdir(File::Spec->abs2rel($path, $root));
    # A shebang supplies the source filename implicitly. Evaluate only
    # its explicit options, without confusing the following file body
    # with standard-input interpreter source.
    scan("$1 source-file", $relative, 0, 0, 1) if $text =~ /\A#!([^\n]*)/;
    my $cmake = $path =~ /(?:\.cmake|CMakeLists\.txt)\z/i ? 1 : 0;
    scan($text, $relative, $cmake, 0, 1);
}
for my $issue (@violations) {
    # Never print source operands: configuration can contain credentials.
    print STDERR "shell-policy: $issue->{path}:$issue->{line}: $issue->{reason}\n";
}
print 'shell-policy: ' . scalar(@files) . " source file(s), "
    . scalar(@violations) . " policy violation(s)\n";
exit(@violations ? 1 : 0);

sub documentation_commands {
    my ($text) = @_;
    my ($fence, $enabled) = ('', 0);
    my @kept;
    for my $line (split /\n/, $text, -1) {
        if (!$fence && $line =~ /^ {0,3}(`{3,}|~{3,})\s*([A-Za-z0-9_-]*)\s*$/) {
            my ($marker, $language) = ($1, lc $2);
            $fence = $marker;
            $enabled = $language =~ /\A(?:sh|shell|shell-session|bash|ksh|zsh|fish|console|powershell|ps1|cmd|bat|make|makefile|cmake|yaml|yml)\z/;
            push @kept, '';
        } elsif ($fence && $line =~ /^ {0,3}(`{3,}|~{3,})\s*$/
            && substr($1, 0, 1) eq substr($fence, 0, 1) && length($1) >= length($fence)) {
            ($fence, $enabled) = ('', 0);
            push @kept, '';
        } else {
            push @kept, $enabled ? $line : '';
        }
    }
    return join "\n", @kept;
}

# Preserve newline tokens and line numbers. Quotes group operands; only
# escaped quotes and command continuations affect this lexical pass.
sub tokens {
    my ($text, $first_line) = @_;
    my @tokens;
    my ($word, $quote, $start) = ('', '', $first_line);
    my $line = $first_line;
    for (my $at = 0; $at < length $text; ++$at) {
        my $char = substr($text, $at, 1);
        my $next = substr($text, $at + 1, 1);
        if (!$quote && $char eq '$'
            && substr($text, $at) =~ /\A(\$\((?:PERL(?:_EXECUTABLE)?|PYTHON(?:_EXECUTABLE)?|Python[0-9]*_EXECUTABLE)\))/) {
            my $variable = $1;
            $start = $line if $word eq '';
            $word .= $variable;
            $at += length($variable) - 1;
            next;
        }
        if (($char eq '\\' || $char eq '`') && $next eq "\n") {
            ++$at; ++$line; next;
        }
        if ($quote) {
            if ($char eq '\\' && $next eq $quote) { $word .= $next; ++$at; }
            elsif ($char eq $quote) { $quote = ''; }
            else { $word .= $char; ++$line if $char eq "\n"; }
            next;
        }
        if ($char eq q{"} || $char eq q{'}) {
            $start = $line if $word eq '';
            $quote = $char;
        } elsif ($char eq '#' && $word eq '') {
            ++$at while $at < length($text) && substr($text, $at, 1) ne "\n";
            --$at;
        } elsif ($char =~ /[\s;|&<>()\[\],]/) {
            push @tokens, {text => $word, line => $start} if length $word;
            $word = '';
            push @tokens, {text => $char, line => $line} if $char !~ /[ \t\r]/;
            ++$line if $char eq "\n";
        } else {
            $start = $line if $word eq '';
            $word .= $char;
        }
    }
    push @tokens, {text => $word, line => $start} if length $word;
    return @tokens;
}

sub interpreter {
    my ($text) = @_;
    $text =~ s/\A\@(?=(?:.*[\\\/])?(?:perl|python|pypy))//i;
    return 'perl' if $text =~ /\A(?:.*[\\\/])?perl(?:[0-9]+(?:\.[0-9]+)*)?(?:\.exe)?\z/i
        || $text =~ /\A(?:\${1,2}\{?PERL(?:_EXECUTABLE)?\}?|\${1,2}\(PERL(?:_EXECUTABLE)?\)|%PERL(?:_EXECUTABLE)?%)\z/;
    return 'python' if $text =~ /\A(?:.*[\\\/])?(?:python|pypy)(?:[0-9]+(?:\.[0-9]+)*)?(?:\.exe)?\z/i
        || $text =~ /\A\$\{?Python[0-9]*_EXECUTABLE\}?\z/;
    return;
}

sub command_position {
    my ($tokens, $at) = @_;
    return 1 if $at == 0;
    my $before = $tokens->[$at - 1]{text};
    return 1 if $before eq '$' && ($at < 2 || $tokens->[$at - 2]{text} eq "\n");
    return 1 if $before =~ /\A(?:\n|;|\||&|COMMAND|run:|command:|entrypoint:|commandLine|RUN|exec|command|sudo|nohup|call)\z/;
    return 1 if $before eq '[' && $at >= 2
        && $tokens->[$at - 2]{text} =~ /\A(?:COMMAND|commandLine|command:|entrypoint:|RUN)\z/;
    return 1 if $before eq 'shell' && $at >= 3
        && $tokens->[$at - 2]{text} eq '(' && $tokens->[$at - 3]{text} eq '$';
    return 1 if $before eq '(' && ($at < 2 || $tokens->[$at - 2]{text} =~ /\A(?:\$|\n|;|\||&)\z/);
    return 1 if $before eq '-' && ($at < 2 || $tokens->[$at - 2]{text} eq "\n");
    # env may precede assignments, options or an absolute executable.
    for (my $back = $at - 1; $back >= 0; --$back) {
        my $part = $tokens->[$back]{text};
        return 1 if $part =~ /\A(?:.*[\\\/])?(?:env|command|sudo|nohup|exec|cmd(?:\.exe)?)\z/i;
        last unless $part =~ /\A(?:[-\/][A-Za-z-]+|[A-Za-z_][A-Za-z0-9_]*=.*)\z/;
    }
    return 0;
}

sub scan {
    my ($text, $path, $cmake, $depth, $first_line) = @_;
    return if $depth > 4;
    my @tokens = tokens($text, $first_line);
    for (my $at = 0; $at < @tokens; ++$at) {
        my $token = $tokens[$at];
        next unless command_position(\@tokens, $at);
        if ($token->{text} =~ /\A--split-string=(.*)/s) {
            scan($1, $path, $cmake, $depth + 1, $token->{line});
            next;
        }
        if ($token->{text} =~ /\s/ && $token->{text} =~ /\A(?:[^\s]*[\\\/])?(?:perl|python[0-9.]*|pypy[0-9.]*|env)\s/) {
            scan($token->{text}, $path, $cmake, $depth + 1, $token->{line});
            next;
        }
        my $language = interpreter($token->{text});
        next unless $language;
        my ($file, $query, $reason, $options_done) = (0, 0, '', 0);
        for (my $arg = $at + 1; $arg < @tokens; ++$arg) {
            my $value = $tokens[$arg]{text};
            next if $value eq ',';
            next if $value eq "\n" && $cmake;
            last if $value =~ /\A(?:\n|;|\||&|\)|>)\z/;
            if ($value eq '<' || $value eq '-') { $reason = 'interpreter source from standard input is forbidden'; last; }
            if ($options_done) { $file = 1; last; }
            if ($value eq '--') { $options_done = 1; next; }
            if ($value =~ /\A(?:--version|--help|-[vh]|-V(?::.*)?)\z/) { $query = 1; next; }
            my ($inline, $operand) = option_source($language, $value);
            if ($inline) {
                $reason = 'inline interpreter source is forbidden'; last;
            }
            if ($operand) {
                ++$arg;
                if ($language eq 'perl' && $operand =~ /[Mm]/ && $arg < @tokens
                    && $tokens[$arg]{text} =~ /[;{}]/) {
                    $reason = 'inline interpreter source is forbidden';
                    last;
                }
                next;
            }
            next if $value =~ /\A-/;
            $file = 1;
            last;
        }
        $reason = 'interpreter source from standard input is forbidden' if !$reason && !$file && !$query;
        push @violations, {path => $path, line => $token->{line}, reason => $reason} if $reason;
    }
    return;
}

# Short switches may be bundled. Operand-taking switches consume the
# remaining token (or the next one), so the E in -MEncode is module
# data, while -npe and -we still expose an inline-program switch.
sub option_source {
    my ($language, $option) = @_;
    return (1, 0) if $option =~ /\A--(?:eval|execute)(?:=|\z)/;
    return (0, 0) unless $option =~ /\A-([^-].*)\z/;
    my $flags = $1;
    for (my $at = 0; $at < length $flags; ++$at) {
        my $flag = substr($flags, $at, 1);
        return (1, 0) if ($language eq 'perl' && $flag =~ /[eE]/)
            || ($language eq 'python' && $flag eq 'c');
        return (1, 0) if $language eq 'perl' && $flag eq 'x';
        return (0, 0) if $language eq 'perl'
            && ($flag eq 'i' || ($flag eq 'd' && substr($flags, $at + 1, 1) eq ':'));
        if (($language eq 'perl' && $flag =~ /[IMmFD]/)
            || ($language eq 'python' && $flag =~ /[mWX]/)) {
            my $payload = substr($flags, $at + 1);
            return (1, 0) if $language eq 'perl' && $flag =~ /[Mm]/ && $payload =~ /[;{}]/;
            return (0, $payload eq '' ? $flag : 0);
        }
    }
    return (0, 0);
}
