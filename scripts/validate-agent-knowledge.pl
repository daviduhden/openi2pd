#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: Agent Knowledge Validation contract 1.0.0.
#
# Read-only Git/instruction checks. Configuration is typed data, never
# executable code. Git operands use list-form process creation; stdout
# is drained while stderr uses a private temporary regular file. This
# preserves separate streams without nonportable pipe select() calls.

use 5.016;
use strict;
use warnings;
use Cwd qw(abs_path getcwd);
use Encode qw(decode encode FB_CROAK FB_DEFAULT);
use File::Basename qw(basename dirname);
use File::Find ();
use File::Spec;
use File::Temp ();
use FindBin qw($Bin);
use Getopt::Long qw(GetOptionsFromArray);
use IPC::Open3;
use JSON::PP;
use lib "$Bin/lib";
use Openi2pd::AgentKnowledge::TOML;

use constant CONTRACT_VERSION => '1.0.0';
use constant SCHEMA_VERSION => '1.0';
use constant CONFIG_NAME => '.agent-knowledge-validator.toml';

my %instruction_names = map { $_ => 1 } qw(
  AGENTS.md AGENT.md SKILL.md CLAUDE.md GEMINI.md
  copilot-instructions.md .cursorrules .windsurfrules
);
my @infrastructure = (
    'scripts/validate-agent-knowledge.pl', '**/scripts/validate-agent-knowledge.pl',
    'scripts/lib/Openi2pd/AgentKnowledge/**', '**/scripts/lib/Openi2pd/AgentKnowledge/**',
    'tests/agent_knowledge_validator/**', '**/tests/agent_knowledge_validator/**',
    '.github/workflows/agent-knowledge-validation.yml', '**/agent-knowledge-validation.yml',
    '**/agent-knowledge-validation.yaml',
);
my @default_excluded = ( '__pycache__/**', '**/__pycache__/**' );

# Perl directory/process APIs return native bytes on POSIX. Keep text
# Unicode internally and encode only at filesystem/process boundaries.
sub fs_text {
    my ($value) = @_;
    return utf8::is_utf8($value) ? $value : decode( 'UTF-8', $value, FB_DEFAULT );
}

sub fs_native {
    my ($value) = @_;
    return $^O eq 'MSWin32' || !utf8::is_utf8($value) ? $value : encode( 'UTF-8', $value );
}

sub trim {
    my ($text) = @_;
    $text =~ s/\A\s+|\s+\z//g;
    return $text;
}

sub read_utf8 {
    my ( $path, $preserve_newlines ) = @_;
    open my $file, '<:raw', fs_native($path) or die "cannot read $path: $!\n";
    local $/ = undef;
    my $bytes = <$file>;
    close $file or die "cannot close $path: $!\n";
    my $text = decode( 'UTF-8', $bytes // '', FB_CROAK );
    # Match pathlib.read_text's universal-newline behavior.
    $text =~ s/\r\n?/\n/g unless $preserve_newlines;
    return $text;
}

sub maybe_read {
    my ($path) = @_;
    my $text = eval { read_utf8($path) };
    return $@ ? undef : $text;
}

sub git {
    my ( $root, $check, @arguments ) = @_;
    my ( $input, $output );
    my $error = File::Temp->new( TEMPLATE => 'openi2pd-agent-git-XXXXXX', TMPDIR => 1, UNLINK => 1 );
    binmode $error, ':raw';
    my @command = map { fs_native($_) } ( 'git', '--no-optional-locks', '--literal-pathspecs',
        '-c', 'core.fsmonitor=false', '-C', $root, @arguments );
    my $pid = eval { open3( $input, $output, '>&' . fileno($error), @command ) };
    die "cannot execute git: $@\n" if !$pid;
    close $input;
    binmode $output, ':raw';
    my $stdout_bytes = '';
    while (1) {
        my $count = sysread $output, my $chunk, 65536;
        if ( !defined $count ) {
            next if $!{EINTR};
            my $failure = "$!";
            kill 'KILL', $pid;
            close $output;
            close $error;
            reap_git($pid);
            die "cannot read git output: $failure\n";
        }
        last unless $count;
        $stdout_bytes .= $chunk;
    }
    close $output;
    my $status = reap_git($pid);
    seek $error, 0, 0 or die "cannot rewind git diagnostic capture: $!\n";
    my $stderr_bytes = do { local $/ = undef; <$error> };
    close $error;
    my $stdout = decode( 'UTF-8', $stdout_bytes, FB_DEFAULT );
    my $stderr = decode( 'UTF-8', $stderr_bytes // '', FB_DEFAULT );
    if ( $check && $status ) {
        my $detail = trim($stderr) || 'git ' . join( ' ', @arguments ) . ' failed';
        die "$detail\n";
    }
    return $stdout;
}

sub reap_git {
    my ($pid) = @_;
    while (1) {
        my $result = waitpid $pid, 0;
        return $? if $result == $pid;
        next if $result < 0 && $!{EINTR};
        die "cannot reap git child: $!\n";
    }
}

sub relative_path {
    my ( $root, $path ) = @_;
    my $relative = File::Spec->abs2rel( $path, $root );
    $relative =~ s{\\}{/}g if $^O eq 'MSWin32';
    return $relative;
}

# Resolve every existing symlink, including one before a '..' component.
# Missing suffixes are preserved, so escape checks also cover broken links.
sub resolved_path {
    my ( $path, $depth ) = @_;
    $depth //= 0;
    die "symlink resolution limit exceeded\n" if $depth > 40;
    my $absolute = File::Spec->rel2abs($path);
    my ( $volume, $directory ) = File::Spec->splitpath( $absolute, 1 );
    my @pending = File::Spec->splitdir($directory);
    my @parts;
    while (@pending) {
        my $part = shift @pending;
        next if $part eq '' || $part eq '.';
        if ( $part eq '..' ) { pop @parts if @parts; next; }
        push @parts, $part;
        my $candidate = File::Spec->catpath( $volume, File::Spec->catdir( File::Spec->rootdir, @parts ), '' );
        if ( -l fs_native($candidate) ) {
            my $target = readlink fs_native($candidate);
            die "cannot read symlink $candidate: $!\n" unless defined $target;
            $target = fs_text($target);
            $target = File::Spec->catfile( dirname($candidate), $target ) unless File::Spec->file_name_is_absolute($target);
            return resolved_path( File::Spec->catfile( $target, @pending ), $depth + 1 );
        }
    }
    return File::Spec->catpath( $volume, File::Spec->catdir( File::Spec->rootdir, @parts ), '' );
}

sub glob_match {
    my ( $path, $pattern ) = @_;
    # fnmatchcase treats slash and leading dots like ordinary characters.
    # Dynamic matching avoids regex backtracking on configured globs and
    # preserves empty/reversed bracket ranges without regex injection.
    my @tokens;
    for ( my $at = 0; $at < length $pattern; ++$at ) {
        my $char = substr( $pattern, $at, 1 );
        if ( $char eq '*' ) { push @tokens, [ 'star' ] unless @tokens && $tokens[-1][0] eq 'star'; }
        elsif ( $char eq '?' ) { push @tokens, [ 'any' ]; }
        elsif ( $char eq '[' ) {
            my $end = $at + 1;
            ++$end if substr( $pattern, $end, 1 ) eq '!';
            ++$end if substr( $pattern, $end, 1 ) eq ']';
            ++$end while $end < length($pattern) && substr( $pattern, $end, 1 ) ne ']';
            if ( $end == length $pattern ) { push @tokens, [ 'literal', '[' ]; }
            else {
                my $class = substr( $pattern, $at + 1, $end - $at - 1 );
                my $negative = $class =~ s/\A!//;
                my @characters = split //, $class;
                my @ranges;
                for ( my $index = 0; $index < @characters; ++$index ) {
                    if ( $index + 2 < @characters && $characters[ $index + 1 ] eq '-' ) {
                        push @ranges, [ ord( $characters[$index] ), ord( $characters[ $index + 2 ] ) ];
                        $index += 2;
                    }
                    else { push @ranges, [ ord( $characters[$index] ), ord( $characters[$index] ) ]; }
                }
                push @tokens, [ 'class', $negative, \@ranges ];
                $at = $end;
            }
        }
        else { push @tokens, [ 'literal', $char ]; }
    }
    my @characters = split //, $path;
    my @reachable = ( 1, (0) x @characters );
    for my $token (@tokens) {
        if ( $token->[0] eq 'star' ) {
            for my $index ( 1 .. @characters ) { $reachable[$index] ||= $reachable[ $index - 1 ]; }
            next;
        }
        my @next = (0) x ( @characters + 1 );
        for my $index ( 0 .. $#characters ) {
            next unless $reachable[$index];
            my $matches = $token->[0] eq 'any'
              || ( $token->[0] eq 'literal' && $characters[$index] eq $token->[1] );
            if ( $token->[0] eq 'class' ) {
                my $number = ord $characters[$index];
                $matches = grep { $number >= $_->[0] && $number <= $_->[1] } @{ $token->[2] };
                $matches = !$matches if $token->[1];
            }
            $next[ $index + 1 ] = 1 if $matches;
        }
        @reachable = @next;
    }
    return $reachable[-1] ? 1 : 0;
}

sub matches_any {
    my ( $path, $patterns ) = @_;
    for my $pattern ( @{$patterns} ) { return 1 if glob_match( $path, $pattern ); }
    return 0;
}

sub is_instruction {
    my ( $path, $extra ) = @_;
    my @parts = split m{/}, $path;
    return 1 if $instruction_names{ $parts[-1] } || $parts[-1] =~ /(?:\.agent\.md|\.instructions\.md|\.mdc)\z/;
    return 1 if @parts >= 2 && $parts[-2] eq 'rules' && grep { /\A\.(?:cursor|windsurf|continue)\z/ } @parts;
    return 1 if $path eq '.agents/skills/README.md';
    return matches_any( $path, $extra );
}

sub discover_instructions {
    my ( $root, $extra ) = @_;
    my @found;
    File::Find::find( {
        no_chdir => 1,
        wanted => sub {
            my $path = fs_text($File::Find::name);
            if ( -l fs_native($path) || ( -d fs_native($path) && basename($path) eq '.git' ) ) {
                $File::Find::prune = 1;
                return;
            }
            return unless -f fs_native($path);
            my $relative = relative_path( $root, $path );
            push @found, $relative if is_instruction( $relative, $extra );
            return;
        },
    }, fs_native($root) );
    return [ sort @found ];
}

sub safe_config_path {
    my ($path) = @_;
    return $path !~ m{\A/} && !grep { $_ eq '..' } split m{/}, $path;
}

sub string_array {
    my ( $node, $allow_empty ) = @_;
    return 0 unless $node && $node->{type} eq 'array';
    for my $value ( @{ $node->{value} } ) {
        return 0 unless $value->{type} eq 'string' && ( $allow_empty || length trim( $value->{value} ) );
    }
    return 1;
}

sub parse_config {
    my ($root) = @_;
    my $config = {
        exclude_paths => [], instruction_globs => [], required_headings => {},
        required_phrases => {}, strict_text => {}, skill_reference_prefixes => [],
        inline_path_checks => 0, minimum_changed_lines => 1,
    };
    my $path = File::Spec->catfile( $root, CONFIG_NAME );
    return $config unless -e fs_native($path);
    my $parsed = eval { Openi2pd::AgentKnowledge::TOML->parse( read_utf8($path) ) };
    die CONFIG_NAME . ": $@" if $@;
    my $top = $parsed->{value};
    die CONFIG_NAME . ": only the [validator] table is supported\n" if grep { $_ ne 'validator' } keys %{$top};
    my $table = $top->{validator} // { type => 'table', value => {} };
    die CONFIG_NAME . ": [validator] must be a table\n" unless $table->{type} eq 'table';
    my $settings = $table->{value};
    my %allowed = map { $_ => 1 } ( 'contract_version', keys %{$config} );
    my @unknown = sort grep { !$allowed{$_} } keys %{$settings};
    die CONFIG_NAME . ': unknown setting(s): ' . join( ', ', @unknown ) . "\n" if @unknown;
    if ( exists $settings->{contract_version} ) {
        my $version = $settings->{contract_version};
        die CONFIG_NAME . ": contract_version must be '" . CONTRACT_VERSION . "'\n"
          unless $version->{type} eq 'string' && $version->{value} eq CONTRACT_VERSION;
    }
    for my $key (qw(exclude_paths instruction_globs)) {
        next unless exists $settings->{$key};
        die CONFIG_NAME . ": $key must be an array of non-empty strings\n" unless string_array( $settings->{$key}, 0 );
        for my $item ( @{ $settings->{$key}{value} } ) {
            my $entry = $item->{value};
            die CONFIG_NAME . ": $key entries must be repository-relative: $entry\n" unless safe_config_path($entry);
            push @{ $config->{$key} }, $entry;
        }
    }
    for my $key (qw(required_headings required_phrases strict_text)) {
        next unless exists $settings->{$key};
        my $mapping = $settings->{$key};
        die CONFIG_NAME . ": $key must be a table" . ( $key eq 'strict_text' ? '' : ' of paths to arrays' ) . "\n"
          unless $mapping->{type} eq 'table';
        for my $relative ( sort keys %{ $mapping->{value} } ) {
            die CONFIG_NAME . ": $key paths must be repository-relative: $relative\n" unless safe_config_path($relative);
            my $values = $mapping->{value}{$relative};
            die CONFIG_NAME . ": $key.$relative must be an array of non-empty strings\n" unless string_array( $values, $key eq 'strict_text' );
            my @strings = map { $_->{value} } @{ $values->{value} };
            if ( $key eq 'strict_text' ) {
                my %known = map { $_ => 1 } qw(ascii_only lf_only final_newline no_trailing_whitespace no_nul);
                die CONFIG_NAME . ": strict_text.$relative has an unknown rule\n" if grep { !$known{$_} } @strings;
            }
            $config->{$key}{$relative} = \@strings;
        }
    }
    my %known = map { $_ => 1 } @{ discover_instructions( $root, $config->{instruction_globs} ) };
    for my $key (qw(required_headings required_phrases)) {
        my @missing = sort grep { !$known{$_} } keys %{ $config->{$key} };
        die CONFIG_NAME . ": $key references unknown instruction path(s): " . join( ', ', @missing ) . "\n" if @missing;
    }
    if ( exists $settings->{skill_reference_prefixes} ) {
        die CONFIG_NAME . ": skill_reference_prefixes must be an array\n" unless string_array( $settings->{skill_reference_prefixes}, 1 );
        my @prefixes = map { $_->{value} } @{ $settings->{skill_reference_prefixes}{value} };
        die CONFIG_NAME . ": skill_reference_prefixes must be an array\n" if grep { !length } @prefixes;
        $config->{skill_reference_prefixes} = \@prefixes;
    }
    if ( exists $settings->{inline_path_checks} ) {
        die CONFIG_NAME . ": inline_path_checks must be a boolean\n" unless $settings->{inline_path_checks}{type} eq 'boolean';
        $config->{inline_path_checks} = $settings->{inline_path_checks}{value};
    }
    if ( exists $settings->{minimum_changed_lines} ) {
        my $node = $settings->{minimum_changed_lines};
        die CONFIG_NAME . ": minimum_changed_lines must be an integer from 1 to 1000\n"
          unless $node->{type} eq 'integer' && $node->{value} >= 1 && $node->{value} <= 1000;
        $config->{minimum_changed_lines} = $node->{value};
    }
    return $config;
}

sub resolve_commit {
    my ( $root, $revision ) = @_;
    my $commit = eval { trim( git( $root, 1, 'rev-parse', '--verify', '--end-of-options', "${revision}^{commit}" ) ) };
    die "revision does not resolve to a commit: $revision\n" if $@ || !$commit;
    return $commit;
}

sub discover_changes {
    my ( $root, $args ) = @_;
    my $raw;
    if ( $args->{scope} eq 'worktree' ) {
        $raw = git( $root, 1, 'diff', '--no-ext-diff', '--no-textconv', '--name-status', '-z', '--no-renames', 'HEAD' );
    }
    else {
        my $base = resolve_commit( $root, $args->{base} );
        my $head = resolve_commit( $root, $args->{head} );
        my $merge = trim( git( $root, 1, 'merge-base', $base, $head ) );
        $args->{merge_base} = $merge;
        $args->{head_commit} = $head;
        $raw = git( $root, 1, 'diff', '--no-ext-diff', '--no-textconv', '--name-status', '-z', '--no-renames', $merge, $head );
    }
    my @fields = split /\0/, $raw, -1;
    my %changes;
    while ( @fields >= 2 ) {
        my ( $status, $path ) = splice @fields, 0, 2;
        $changes{$path} = { status => substr( $status, 0, 1 ), path => $path } if length($status) && length($path);
    }
    if ( $args->{scope} eq 'worktree' ) {
        for my $path ( split /\0/, git( $root, 1, 'ls-files', '--others', '--exclude-standard', '-z' ) ) {
            $changes{$path} = { status => 'A', path => $path } if length $path;
        }
    }
    return [ map { $changes{$_} } sort keys %changes ];
}

sub normalize_line {
    my ($line) = @_;
    $line = trim($line);
    return '' if !length($line) || $line =~ /\A(?:\#|<!--|-->)/;
    return '' if $line =~ /\A(?:updated?|generated|last[ -]updated|date|updated_at)\s*[:=]/i;
    return '' if $line =~ /\A[-*_`~>| ]+\z/;
    $line =~ s/\s+/ /g;
    return trim($line);
}

sub changed_content {
    my ( $root, $args, $path ) = @_;
    my @revisions = $args->{scope} eq 'range' ? ( $args->{merge_base}, $args->{head_commit} ) : ('HEAD');
    my $diff = git( $root, 1, 'diff', '--no-ext-diff', '--no-textconv', '--unified=0', @revisions, '--', $path );
    my ( @added, @removed );
    for my $line ( split /\R/, $diff ) {
        next if $line =~ /\A(?:\+\+\+|---)/;
        next unless $line =~ /\A([+-])(.*)\z/s;
        my ( $sign, $content ) = ( $1, normalize_line($2) );
        next unless length $content;
        push @{ $sign eq '+' ? \@added : \@removed }, $content;
    }
    if ( $args->{scope} eq 'worktree' && !length($diff) && -f fs_native( File::Spec->catfile( $root, $path ) )
        && !length git( $root, 0, 'ls-files', '--error-unmatch', '--', $path ) ) {
        my $text = maybe_read( File::Spec->catfile( $root, $path ) );
        push @added, grep { length } map { normalize_line($_) } split /\R/, $text if defined $text;
    }
    return ( \@added, \@removed );
}

sub substantive_update {
    my ( $added, $removed, $minimum ) = @_;
    my @new = @{$added};
    my @old;
    OLD: for my $line ( @{$removed} ) {
        for my $index ( 0 .. $#new ) {
            if ( $new[$index] eq $line ) { splice @new, $index, 1; next OLD; }
        }
        push @old, $line;
    }
    my $characters = 0;
    for my $line ( @new, @old ) { $characters += ( $line =~ tr/A-Za-z0-9/A-Za-z0-9/ ); }
    return @new + @old >= $minimum && $characters >= 24;
}

sub has_heading {
    my ( $text, $heading ) = @_;
    return $text =~ /^\#{2,}\s+\Q$heading\E\s*$/m;
}

sub frontmatter {
    my ($text) = @_;
    return undef unless $text =~ /\A---\s*\n(.*?)\n---\s*(?:\n|\z)/s;
    my %fields;
    for my $line ( split /\R/, $1 ) {
        next unless $line =~ /^([A-Za-z0-9_-]+):\s*(.*?)\s*$/;
        my ( $key, $value ) = ( $1, $2 );
        $value =~ s/\A["']+|["']+\z//g;
        $fields{$key} = $value;
    }
    return \%fields;
}

sub validate_structure {
    my ( $root, $paths, $config ) = @_;
    my @errors;
    for my $relative ( @{$paths} ) {
        my $text = maybe_read( File::Spec->catfile( $root, $relative ) );
        if ( !defined $text ) { push @errors, [ $relative, 'cannot read UTF-8 instruction file' ]; next; }
        if ( !length trim($text) ) { push @errors, [ $relative, 'instruction file is empty' ]; next; }
        for my $heading ( @{ $config->{required_headings}{$relative} // [] } ) {
            push @errors, [ $relative, "missing configured section: $heading" ] unless has_heading( $text, $heading );
        }
        for my $phrase ( @{ $config->{required_phrases}{$relative} // [] } ) {
            ( my $normalized = $text ) =~ s/\s+/ /g;
            ( my $expected = $phrase ) =~ s/\s+/ /g;
            push @errors, [ $relative, "missing configured policy text: $phrase" ] if index( $normalized, $expected ) < 0;
        }
        my $name = basename($relative);
        if ( $relative eq 'AGENTS.md' ) {
            push @errors, [ $relative, 'missing Continuous Agent & Skills Maintenance section' ]
              unless has_heading( $text, 'Continuous Agent & Skills Maintenance' );
        }
        elsif ( $name eq 'SKILL.md' ) {
            my $fields = frontmatter($text);
            if ( !defined $fields ) { push @errors, [ $relative, 'missing or malformed YAML frontmatter delimiters' ]; }
            else {
                for my $key (qw(name description)) {
                    push @errors, [ $relative, "frontmatter requires non-empty $key" ] unless length trim( $fields->{$key} // '' );
                }
                push @errors, [ $relative, 'frontmatter name must match the skill directory' ]
                  if $fields->{name} && trim( $fields->{name} ) ne basename( dirname($relative) );
            }
            push @errors, [ $relative, 'missing Maintenance & Continuous Learning section' ]
              unless has_heading( $text, 'Maintenance & Continuous Learning' );
        }
        elsif ( $name =~ /\.agent\.md\z/ ) {
            if ( $text =~ /\A---/ ) {
                my $fields = frontmatter($text);
                if ( !defined $fields ) { push @errors, [ $relative, 'malformed YAML frontmatter delimiters' ]; }
                elsif ( !length trim( $fields->{name} // '' ) || !length trim( $fields->{description} // '' ) ) {
                    push @errors, [ $relative, 'agent frontmatter requires name and description' ];
                }
            }
            push @errors, [ $relative, 'missing Post-Change Knowledge Synchronization section' ]
              unless has_heading( $text, 'Post-Change Knowledge Synchronization' );
        }
    }
    return \@errors;
}

sub validate_extensions {
    my ( $root, $paths, $config, $skills ) = @_;
    my @errors;
    for my $relative ( @{$paths} ) {
        my $text = maybe_read( File::Spec->catfile( $root, $relative ) );
        next unless defined $text;
        for my $pattern ( sort keys %{ $config->{strict_text} } ) {
            next unless glob_match( $relative, $pattern );
            my %rules = map { $_ => 1 } @{ $config->{strict_text}{$pattern} };
            # Universal newline decoding is correct for prose parsing,
            # but the LF rule must inspect the original decoded bytes.
            my $raw_text = $rules{lf_only} ? eval { read_utf8( File::Spec->catfile( $root, $relative ), 1 ) } : '';
            push @errors, [ $relative, 'configured text rule requires LF line endings' ] if $rules{lf_only} && defined($raw_text) && $raw_text =~ /\r/;
            push @errors, [ $relative, 'configured text rule requires a final newline' ] if $rules{final_newline} && length($text) && $text !~ /\n\z/;
            push @errors, [ $relative, 'configured text rule forbids trailing whitespace' ] if $rules{no_trailing_whitespace} && $text =~ /[\t ]+$/m;
            push @errors, [ $relative, 'configured text rule forbids NUL bytes' ] if $rules{no_nul} && index( $text, "\0" ) >= 0;
            push @errors, [ $relative, 'configured text rule requires ASCII text' ] if $rules{ascii_only} && $text =~ /[^\x00-\x7f]/;
        }
        if ( $relative =~ /\.agent\.md\z/ && @{ $config->{skill_reference_prefixes} } ) {
            while ( $text =~ /`([^`]+)`/g ) {
                my $reference = $1;
                if ( ( grep { index( $reference, $_ ) == 0 } @{ $config->{skill_reference_prefixes} } ) && !$skills->{$reference} ) {
                    push @errors, [ $relative, "references missing skill: $reference" ];
                }
            }
        }
        if ( $config->{inline_path_checks} ) {
            while ( $text =~ /`([^`]+)`/g ) {
                my $target = $1;
                next if $target =~ m{\A/} || $target =~ /\s/;
                next unless basename($target) =~ /.\.(?:md|pl|pm|t|8|ignore)\z/ || $target eq 'Makefile' || $target eq 'LICENSE';
                unless ( -e fs_native( File::Spec->catfile( dirname( File::Spec->catfile( $root, $relative ) ), $target ) ) || -e fs_native( File::Spec->catfile( $root, $target ) ) ) {
                    push @errors, [ $relative, "inline repository path does not exist: $target" ];
                }
            }
        }
    }
    return \@errors;
}

sub validate_references {
    my ( $root, $paths ) = @_;
    my @errors;
    for my $relative ( @{$paths} ) {
        my $file = File::Spec->catfile( $root, $relative );
        my $text = maybe_read($file);
        next unless defined $text;
        while ( $text =~ /(?<!!)\[[^\]]*\]\(([^)]+)\)/g ) {
            my $target = trim($1);
            my ($destination) = split /\s+/, $target, 2;
            $destination =~ s/\A[<>]+|[<>]+\z//g;
            next if $destination =~ /\A[A-Za-z][A-Za-z0-9+.-]*:/ || $destination =~ m{\A//[^/]};
            ( my $local = $destination ) =~ s/[?#].*\z//s;
            next unless length $local;
            $local = encode( 'UTF-8', $local );
            $local =~ s/%([0-9a-fA-F]{2})/pack 'C', hex $1/ge;
            # urllib.unquote replaces invalid percent-encoded UTF-8.
            $local = decode( 'UTF-8', $local, FB_DEFAULT );
            my $candidate = $local =~ s{\A/+}{} ? File::Spec->catfile( $root, $local ) : File::Spec->catfile( dirname($file), $local );
            my $resolved = resolved_path($candidate);
            my $inside = relative_path( $root, $resolved );
            if ( File::Spec->file_name_is_absolute($inside) || $inside =~ m{\A\.\.(?:/|\z)} ) {
                push @errors, [ $relative, "local reference escapes repository: $destination" ];
            }
            elsif ( !-e fs_native($resolved) ) { push @errors, [ $relative, "broken local reference: $destination" ]; }
        }
    }
    return \@errors;
}

sub duplicates_and_conflicts {
    my ( $root, $paths ) = @_;
    my ( %content, @conflicts );
    for my $relative ( @{$paths} ) {
        my $text = maybe_read( File::Spec->catfile( $root, $relative ) );
        next unless defined $text;
        ( my $normalized = $text ) =~ s/\s+/ /g;
        $normalized = trim($normalized);
        push @{ $content{$normalized} }, $relative if length $normalized;
        my %actions;
        for my $line ( split /\R/, $text ) {
            next unless $line =~ /^\s*(.*?)\b(must not|mustn't|must|should not|should|do not|don't|never|always)\b\s+([^.!?]{12,})[.!?]?\s*$/i;
            my ( $prefix, $modal, $action ) = ( $1, lc $2, $3 );
            $action = lc "$prefix $action";
            $action =~ s/[^a-z0-9]+/ /g;
            $action = trim($action);
            next if length($action) < 12;
            my $polarity = $modal =~ /\A(?:must not|mustn't|should not|do not|don't|never)\z/ ? 'negative' : 'positive';
            $actions{$action}{$polarity} = 1;
        }
        for my $action ( sort keys %actions ) {
            push @conflicts, "$relative: explicitly opposite directives for '$action'" if keys %{ $actions{$action} } == 2;
        }
    }
    my @duplicates = sort map { join ', ', sort @{$_} } grep { @{$_} > 1 } values %content;
    return ( \@duplicates, \@conflicts );
}

sub result {
    my ( $id, $status, $message, $files, $remediation ) = @_;
    my %unique = map { $_ => 1 } @{ $files // [] };
    return { rule_id => $id, status => $status, message => $message, files => [ sort keys %unique ], remediation => $remediation };
}

sub error_checks {
    return [
        result( 'AK001', 'PASS', 'Agent knowledge synchronization' ),
        result( 'AK002', 'PASS', 'Agent and skill structure' ),
        result( 'AKX001', 'PASS', 'Configured repository-specific validations' ),
        result( 'AK003', 'PASS', 'Internal references' ),
        result( 'AK004', 'PASS', 'Knowledge update relevance' ),
        result( 'AK005', 'FAIL', 'Invalid validator configuration or execution error', [], 'Check the Git worktree, revisions, and validator configuration.' ),
        result( 'AK006', 'PASS', 'No duplicate or conflicting instructions detected' ),
    ];
}

sub run_validation {
    my ($args) = @_;
    my $native_root = abs_path( fs_native( trim( git( fs_text(getcwd()), 1, 'rev-parse', '--show-toplevel' ) ) ) );
    my $root = defined($native_root) ? fs_text($native_root) : undef;
    die "cannot resolve Git worktree\n" unless defined $root;
    my $config = parse_config($root);
    my $changes = discover_changes( $root, $args );
    my $instructions = discover_instructions( $root, $config->{instruction_globs} );
    my @exclusions = ( @default_excluded, @{ $config->{exclude_paths} } );
    my @considered = grep { !matches_any( $_->{path}, \@exclusions ) } @{$changes};
    my @changed = map { $_->{path} } @considered;
    my $infrastructure_only = @changed && !grep { !matches_any( $_, \@infrastructure ) } @changed;
    my @knowledge = grep { is_instruction( $_, $config->{instruction_globs} ) && -f fs_native( File::Spec->catfile( $root, $_ ) ) } @changed;
    my @project = grep { !is_instruction( $_, $config->{instruction_globs} ) } @changed;
    my ( @substantive, @suspicious );
    for my $path (@knowledge) {
        my ( $added, $removed ) = changed_content( $root, $args, $path );
        push @{ substantive_update( $added, $removed, $config->{minimum_changed_lines} ) ? \@substantive : \@suspicious }, $path;
    }
    my $sync = !@considered || $infrastructure_only
      ? result( 'AK001', 'PASS', 'Agent knowledge synchronization' )
      : @substantive ? result( 'AK001', 'PASS', 'Agent knowledge synchronized', \@substantive )
      : result( 'AK001', 'FAIL', 'Missing agent knowledge update', @project ? \@project : \@changed,
        'Update a relevant AGENTS.md, agent instruction, or SKILL.md with substantive operational guidance.' );
    my $structure_errors = validate_structure( $root, $instructions, $config );
    my $structure = @{$structure_errors}
      ? result( 'AK002', 'FAIL', 'Invalid agent or skill structure', [ map { $_->[0] } @{$structure_errors} ],
        'Fix the required headings and simple frontmatter fields; frontmatter checks are partial and are not full YAML validation.' )
      : result( 'AK002', 'PASS', 'Agent and skill structure valid' );
    my %skills;
    for my $relative ( @{$instructions} ) {
        next unless basename($relative) eq 'SKILL.md';
        my $text = maybe_read( File::Spec->catfile( $root, $relative ) );
        next unless defined $text;
        my $fields = frontmatter($text);
        $skills{ trim( $fields->{name} ) } = 1 if $fields && length trim( $fields->{name} // '' );
    }
    my $extension_errors = validate_extensions( $root, $instructions, $config, \%skills );
    my $extension = @{$extension_errors}
      ? result( 'AKX001', 'FAIL', 'Configured repository-specific validation failed', [ map { $_->[0] } @{$extension_errors} ],
        'Correct the configured rule or update its documented repository-specific configuration.' )
      : result( 'AKX001', 'PASS', 'Configured repository-specific validations passed' );
    my $reference_errors = validate_references( $root, $instructions );
    my $references = @{$reference_errors}
      ? result( 'AK003', 'FAIL', 'Broken internal references', [ map { $_->[0] } @{$reference_errors} ],
        'Correct or remove the broken repository-relative Markdown link.' )
      : result( 'AK003', 'PASS', 'Internal references valid' );
    my $relevance = @suspicious
      ? result( 'AK004', 'FAIL', 'Suspicious knowledge update: no effective content change detected', \@suspicious,
        'Replace cosmetic or metadata-only edits with substantive operational guidance.' )
      : @substantive ? result( 'AK004', 'WARN', 'Manual relevance review recommended', \@substantive,
        'Confirm that the instruction update is relevant and reusable; automation cannot prove semantic correctness.' )
      : result( 'AK004', 'PASS', 'No suspicious knowledge update detected' );
    my ( $duplicates, $conflicts ) = duplicates_and_conflicts( $root, $instructions );
    my @conflict_files = ( map { split /, /, $_ } @{$duplicates} );
    push @conflict_files, map { ( split /:/, $_, 2 )[0] } @{$conflicts};
    my $conflict = @{$duplicates} || @{$conflicts}
      ? result( 'AK006', 'WARN', 'Potential duplicate or conflicting instructions found', \@conflict_files,
        'Review the listed instructions and consolidate or clarify only when their scopes truly overlap.' )
      : result( 'AK006', 'PASS', 'No duplicate or conflicting instructions detected' );
    return [ $sync, $structure, $extension, $references, $relevance,
        result( 'AK005', 'PASS', 'Validator configuration and execution valid' ), $conflict ];
}

sub print_report {
    my ( $checks, $format ) = @_;
    my @errors = grep { $_->{status} eq 'FAIL' } @{$checks};
    my @warnings = grep { $_->{status} eq 'WARN' } @{$checks};
    my $report = {
        schema_version => SCHEMA_VERSION, contract_version => CONTRACT_VERSION,
        status => @errors ? 'FAIL' : 'PASS', checks => $checks,
        errors => \@errors, warnings => \@warnings,
        summary => { errors => scalar @errors, warnings => scalar @warnings },
    };
    if ( $format eq 'json' ) { print JSON::PP->new->canonical->pretty->encode($report); }
    else {
        for my $check ( @{$checks} ) {
            print "[$check->{status}] $check->{rule_id} - $check->{message}\n";
            print '  Files: ' . join( ', ', @{ $check->{files} } ) . "\n" if @{ $check->{files} };
            print "  Action: $check->{remediation}\n" if defined $check->{remediation};
        }
        print "\nAgent Knowledge Validation\nStatus: $report->{status}\nErrors: " . scalar(@errors) . "\nWarnings: " . scalar(@warnings) . "\n";
    }
    return @errors ? 1 : 0;
}

sub usage {
    my ($stream) = @_;
    print {$stream} 'usage: validate-agent-knowledge.pl --scope {worktree,range} [--base BASE] [--head HEAD] [--format {text,json}]' . "\n\n";
    print {$stream} 'Validate Agent Knowledge contract ' . CONTRACT_VERSION . " using only Perl core modules.\n\n";
    print {$stream} "  --scope {worktree,range}  required comparison scope\n  --base BASE              base Git revision; required with --scope range\n  --head HEAD              head Git revision; required with --scope range\n  --format {text,json}     report format (default: text)\n  --help, -h               show this help message\n";
    return;
}

sub main {
    my @arguments = @_;
    my %args = ( format => 'text' );
    # Preserve unique long-option abbreviations, including --sc/--fo.
    # The short -h alias must not turn ambiguous --h into a valid option.
    for my $argument (@arguments) {
        last if $argument eq '--';
        next unless $argument =~ /\A--([^=]+)/;
        my $name = $1;
        my @matching = grep { index( $_, $name ) == 0 } qw(scope base head format help);
        if ( @matching > 1 ) {
            usage(\*STDERR);
            print STDERR "validate-agent-knowledge.pl: error: ambiguous option: --$name\n";
            return 2;
        }
    }
    Getopt::Long::Configure(qw(auto_abbrev no_ignore_case no_getopt_compat));
    my $parsed = GetOptionsFromArray( \@arguments, 'scope=s' => \$args{scope}, 'base=s' => \$args{base},
        'head=s' => \$args{head}, 'format=s' => \$args{format}, 'help|h' => \$args{help} );
    if ( $args{help} && $parsed && !@arguments ) { usage(\*STDOUT); return 0; }
    my $error;
    if ( !$parsed || @arguments ) { $error = 'unrecognized arguments'; }
    elsif ( !defined $args{scope} ) { $error = 'the following arguments are required: --scope'; }
    elsif ( $args{scope} ne 'worktree' && $args{scope} ne 'range' ) { $error = '--scope must be worktree or range'; }
    elsif ( $args{format} ne 'text' && $args{format} ne 'json' ) { $error = '--format must be text or json'; }
    elsif ( $args{scope} eq 'range' && ( !$args{base} || !$args{head} ) ) { $error = '--scope range requires both --base and --head'; }
    elsif ( $args{scope} eq 'worktree' && ( $args{base} || $args{head} ) ) { $error = '--base and --head are only valid with --scope range'; }
    if (defined $error) { usage(\*STDERR); print STDERR "validate-agent-knowledge.pl: error: $error\n"; return 2; }
    my $checks = eval { run_validation(\%args) };
    if ($@) {
        my $detail = trim($@);
        print_report( error_checks(), $args{format} );
        print STDERR "validator execution error: $detail\n" if $args{format} eq 'json';
        return 2;
    }
    return print_report( $checks, $args{format} );
}

binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';
exit main(@ARGV);

__END__

=head1 NAME

validate-agent-knowledge.pl - validate agent knowledge contract 1.0.0

=head1 SYNOPSIS

  perl scripts/validate-agent-knowledge.pl --scope worktree
  perl scripts/validate-agent-knowledge.pl --scope range \
      --base BASE_REF --head HEAD_REF --format json

=head1 CONTRACT

Requires Perl 5.16+, core modules, Git and an existing worktree. Scope
C<worktree> includes staged, unstaged and untracked non-ignored paths.
Scope C<range> compares the revisions' merge base to the selected head.
Both scopes validate instruction files in the current worktree. Renames
are deliberately reported as deletion plus addition.

C<--format text> is the default; C<--format json> emits a single UTF-8
object with schema version 1.0, contract version 1.0.0, ordered checks,
errors, warnings and summary counts. AK001 through AK006 and optional
AKX001 retain their documented meanings. Warnings do not fail validation.
Exit codes are 0 for pass, 1 for policy failure and 2 for usage, Git,
configuration or execution errors. JSON diagnostics go to stderr.

=head1 CONFIGURATION AND SECURITY

C<.agent-knowledge-validator.toml> is optional typed configuration. Its
schema cannot execute commands. The adjacent
C<Openi2pd::AgentKnowledge::TOML> module documents accepted forms and
parsing limits. Source/configuration UTF-8 is strict; malformed Git or
filesystem output is decoded with replacement characters.

Git arguments never pass through a shell. Literal pathspecs preserve
filenames containing wildcard characters. Revisions terminate option
parsing. Optional index writes, fsmonitor hooks, external diff drivers
and text conversion are disabled: validation examines source bytes.
Instruction discovery does not follow symlinks. Relative Markdown links
are percent-decoded and resolved before repository containment checks.

Each Git child owns a stdout pipe and a private temporary stderr file.
The regular file avoids pipe-select dependencies on Windows and stderr
backpressure. The tempfile object removes its file when the invocation
ends, including exceptional return. Children are reaped with EINTR
handling; a failed pipe read kills and reaps only that invocation.

Strict LF validation examines original decoded line endings, while
prose parsing normalizes line endings. Minimum changed lines accepts
integers only, including exclusion of TOML booleans.

=cut
