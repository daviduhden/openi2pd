#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: check agent maintenance.
#
# Enforces a substantive agent/skill update for repository changes and checks
# the structure and local links of maintained instruction files.
#

use strict;
use warnings;
use 5.016;

use File::Basename qw(dirname basename);
use File::Find qw(find);
use File::Spec;
use FindBin qw($Bin);
use Getopt::Long qw(GetOptions);

my ( $base, $head, $help );
GetOptions(
    'base=s' => \$base,
    'head=s' => \$head,
    'help'   => \$help,
) or die usage();
if ($help) {
    print usage();
    exit 0;
}
die "--base and --head must be provided together\n" if defined($base) != defined($head);

my $root = File::Spec->rel2abs(
    File::Spec->catdir( $Bin, File::Spec->updir, File::Spec->updir ) );
my @errors;
my @changed = changed_paths();
my %changed = map { $_ => 1 } @changed;

if (@changed) {
    my @updated_instructions = grep { is_instruction_path($_) && substantive_instruction_change($_) }
      @changed;
    if (!@updated_instructions) {
        push @errors,
          'repository changes require a substantive update to AGENTS.md, an agent instruction, or SKILL.md';
    }
}

my @instruction_files = discover_instruction_files();
my %instruction_text;
for my $relative (@instruction_files) {
    my $path = File::Spec->catfile( $root, split m{/}, $relative );
    open my $fh, '<', $path or do {
        push @errors, "cannot read instruction file $relative: $!";
        next;
    };
    local $/;
    my $content = <$fh>;
    close $fh;
    $instruction_text{$relative} = $content;
    validate_instruction_file( $relative, $content );
}

my $agents = $instruction_text{'AGENTS.md'};
if (!defined $agents) {
    push @errors, 'root AGENTS.md is missing';
}
else {
    my $normalized = normalize_text($agents);
    push @errors, 'AGENTS.md lacks the mandatory Continuous Agent & Skills Maintenance section'
      unless $agents =~ /^## Continuous Agent & Skills Maintenance\s*$/m;
    for my $phrase (
        'Agent instructions and skills are living repository artifacts.',
        'Every repository modification MUST include an agent/skills synchronization step.',
        'Before completing any task, inspect the applicable AGENTS.md and SKILL.md files, update the affected instructions, and validate their consistency with the implementation.',
        'A code change without the corresponding agent knowledge maintenance is considered incomplete.',
        'Never claim completion when the synchronization step has been skipped or has failed.'
      )
    {
        push @errors, "AGENTS.md is missing required policy text: $phrase"
          unless index( $normalized, $phrase ) >= 0;
    }
    for my $item (
        'Repository changes implemented.',
        'Relevant agent instructions reviewed.',
        'Relevant skills reviewed.',
        'Affected AGENTS.md / SKILL.md files updated.',
        'Obsolete knowledge removed.',
        'New reusable knowledge incorporated.',
        'Agent and skill references validated.',
        'Automated maintenance checks passed.',
        'Final synchronization status reported.'
      )
    {
        push @errors, "AGENTS.md Definition of Done is missing: $item"
          unless index( $normalized, $item ) >= 0;
    }
}

for my $relative (@instruction_files) {
    next unless is_specialized_agent_path($relative);
    my $content = $instruction_text{$relative} // '';
    push @errors, "$relative lacks Post-Change Knowledge Synchronization"
      unless $content =~ /^## Post-Change Knowledge Synchronization\s*$/m;
}

if (@errors) {
    print STDERR "agent-maintenance: $_\n" for @errors;
    print STDERR "Structural checks cannot assess whether the update is semantically relevant.\n";
    exit 1;
}

if (@changed) {
    my @paths = grep { is_instruction_path($_) && substantive_instruction_change($_) } @changed;
    print "agent-maintenance: PASS; ", scalar(@changed),
      " changed path(s), substantive instruction update(s): ", join( ', ', @paths ), "\n";
}
else {
    print "agent-maintenance: PASS; no repository changes detected\n";
}
print "Semantic relevance still requires the mandatory task review and final report.\n";
exit 0;

sub usage {
    return <<'USAGE';
Usage: perl tools/scripts/check-agent-maintenance.pl [--base COMMIT --head COMMIT]

With --base/--head, inspect a GitHub pull request's three-dot commit range.
Without them, inspect staged, unstaged, and untracked local changes.
USAGE
}

sub git_output {
    my (@args) = @_;
    open my $fh, '-|', 'git', @args or die "cannot run git @args: $!\n";
    binmode $fh;
    local $/;
    my $output = <$fh> // '';
    close $fh or die "git @args failed\n";
    return $output;
}

sub git_try {
    my (@args) = @_;
    open my $fh, '-|', 'git', @args or return ( 0, '' );
    binmode $fh;
    local $/;
    my $output = <$fh> // '';
    my $ok = close $fh;
    return ( $ok ? 1 : 0, $output );
}

sub changed_paths {
    my @outputs;
    if ( defined $base ) {
        for my $revision ( $base, $head ) {
            my $resolved = git_output( 'rev-parse', '--verify', "$revision^{commit}" );
            die "invalid commit reference '$revision'\n" unless $resolved =~ /^[0-9a-f]+\s*$/i;
        }
        push @outputs,
          git_output( 'diff', '--name-only', '-z', '--no-renames', "$base...$head", '--' );
    }
    else {
        push @outputs, git_output( 'diff', '--name-only', '-z', '--no-renames', 'HEAD', '--' );
        push @outputs, git_output( 'ls-files', '--others', '--exclude-standard', '-z' );
    }
    my %seen;
    return sort grep { length && !$seen{$_}++ } map { split /\0/ } @outputs;
}

sub is_instruction_path {
    my ($path) = @_;
    return 1 if basename($path) eq 'AGENTS.md';
    return 1 if basename($path) eq 'SKILL.md';
    return 1 if basename($path) =~ /\.agent\.md\z/i;
    return 1 if basename($path) =~ /\A(?:CLAUDE|GEMINI|CODY|AIDER)\.md\z/i;
    return 1 if basename($path) =~ /\A(?:\.cursorrules|\.windsurfrules|copilot-instructions\.md)\z/i;
    return 1 if $path =~ m{\A(?:\.agents/(?:agents/|skills/README\.md)|\.github/(?:agents|skills|instructions)/|\.claude/(?:agents|skills)/|\.cursor/rules/|\.continue/rules/)};
    return 1 if $path =~ m{\A\.github/(?:copilot-instructions\.md|instructions/.+\.instructions\.md)\z};
    return 0;
}

sub is_specialized_agent_path {
    my ($path) = @_;
    return 1 if basename($path) =~ /\.agent\.md\z/i;
    return 1 if $path =~ m{\A\.github/agents/};
    return 1 if $path =~ m{\A(?:\.agents/agents|\.claude/agents|\.continue/agents)/};
    return 1 if $path =~ m{\A\.cursor/rules/};
    return 0;
}

sub substantive_instruction_change {
    my ($relative) = @_;
    my $diff;
    if ( defined $base ) {
        $diff = git_output( 'diff', '--no-ext-diff', '--no-renames', '--unified=0', "$base...$head", '--', $relative );
    }
    else {
        $diff = git_output( 'diff', '--no-ext-diff', '--no-renames', '--unified=0', 'HEAD', '--', $relative );
        if ( !length $diff ) {
            my $absolute = File::Spec->catfile( $root, split m{/}, $relative );
            my ( $tracked, $tracked_output ) = git_try( 'ls-files', '--error-unmatch', '--', $relative );
            if ( !$tracked && -f $absolute ) {
                open my $fh, '<', $absolute or return 0;
                local $/;
                my $text = <$fh> // '';
                close $fh;
                $diff = join '', map { "+$_\n" } split /\n/, $text;
            }
        }
    }

    my ( $characters, $words ) = ( 0, 0 );
    for my $line ( split /\n/, $diff ) {
        next unless $line =~ /^[+-](?![+-])/;
        $line =~ s/^[+-]//;
        $line =~ s/<!--.*?-->//g;
        $line =~ s/[^A-Za-z0-9]+/ /g;
        my @words = grep { length >= 2 } split /\s+/, $line;
        $characters += length join '', @words;
        $words += scalar @words;
    }
    return $characters >= 80 && $words >= 8;
}

sub discover_instruction_files {
    my @files;
    find(
        {
            no_chdir => 1,
            wanted   => sub {
                my $absolute = $File::Find::name;
                if ( -d $absolute ) {
                    my $name = basename($absolute);
                    $File::Find::prune = 1
                      if $name eq '.git' || $name eq 'node_modules' || $name eq '.gradle'
                      || $name eq '.cxx' || $name eq 'build-output';
                    return;
                }
                return unless -f $absolute;
                my $relative = File::Spec->abs2rel( $absolute, $root );
                $relative =~ s{\\}{/}g;
                push @files, $relative if is_instruction_path($relative);
            },
        },
        $root
    );
    return sort @files;
}

sub validate_instruction_file {
    my ( $relative, $content ) = @_;
    if ( basename($relative) eq 'SKILL.md' ) {
        my @lines = split /\n/, $content;
        push @errors, "$relative: malformed Agent Skill frontmatter"
          unless @lines >= 4 && $lines[0] eq '---' && $lines[3] eq '---';
        my ($name) = $content =~ /^name: ([a-z0-9-]+)$/m;
        my ($description) = $content =~ /^description: (.+)$/m;
        my $expected_name = basename( dirname(File::Spec->catfile( $root, split m{/}, $relative ) ) );
        push @errors, "$relative: skill name does not match its directory"
          unless defined($name) && $name eq $expected_name;
        push @errors, "$relative: description must start with 'Use when' and stay within 300 characters"
          unless defined($description) && $description =~ /^Use when\b/ && length($description) <= 300;
        for my $section (
            'Activate when', 'Inspect first', 'Reference sources', 'Reconnaissance',
            'Parallel implementation', 'Conventions', 'Known issues and decisions',
            'Do not', 'Prepare for CI', 'Maintenance & Continuous Learning',
            'Tracking', 'References'
          )
        {
            push @errors, "$relative: missing '## $section'"
              unless $content =~ /^## \Q$section\E\s*$/m;
        }
    }
    validate_local_links( $relative, $content );
}

sub validate_local_links {
    my ( $relative, $content ) = @_;
    my $absolute = File::Spec->catfile( $root, split m{/}, $relative );
    while ( $content =~ /\[[^\]]*\]\(([^)]+)\)/g ) {
        my $target = $1;
        $target =~ s/^\s+|\s+$//g;
        $target =~ s/^<([^>]+)>.*$/$1/;
        $target =~ s/\s+["'][^"']*["']\s*$//;
        next if $target =~ m{\A(?:[A-Za-z][A-Za-z0-9+.-]*:|//|#)};
        $target =~ s/[?#].*\z//;
        next unless length $target;
        $target =~ s/%20/ /gi;
        my $candidate = $target =~ m{\A/}
          ? File::Spec->catfile( $root, split m{/}, substr( $target, 1 ) )
          : File::Spec->catfile( dirname($absolute), split m{/}, $target );
        if ( !-e $candidate ) {
            push @errors, "$relative: broken local link '$target'";
        }
    }
}

sub normalize_text {
    my ($text) = @_;
    $text =~ s/\s+/ /g;
    return $text;
}

=head1 NAME

check-agent-maintenance.pl - require agent-knowledge updates with repository changes

=head1 DESCRIPTION

Checks a Git change range (or local worktree) for a substantive update to an
agent instruction or skill. It also checks required skill sections and
frontmatter, specialized-agent synchronization headings, the root maintenance
policy, and local Markdown links in recognized instruction files. Structural
checks cannot prove that a knowledge update is semantically relevant; that
review remains part of the agent's mandatory Definition of Done.

=head1 SECURITY

The checker reads Git history and repository text only. It makes no network
requests, executes no repository code, and reads no credentials.

=cut
