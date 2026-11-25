#!/usr/bin/env perl
# See the LICENSE file at the top of the project tree for copyright
# and license details.

use 5.016;
use strict;
use warnings;

use Carp           qw(croak);
use Cwd            qw(abs_path getcwd);
use File::Basename qw(basename);
use File::Find     ();
use File::Path     qw(make_path);
use File::Spec     ();
use File::Temp     qw(tempdir);

my %section_name = (
    1 => 'General Commands',
    2 => 'System Calls',
    3 => 'Library Functions',
    4 => 'Device Drivers',
    5 => 'File Formats',
    6 => 'Games',
    7 => 'Miscellaneous Information',
    8 => 'System Manager Commands',
    9 => 'Kernel Developer Information',
    l => 'Local Commands',
    n => 'New Commands',
);

# These are operating system and dependency references currently in the
# manuals. Keep them readable offline and reject unknown missing targets.
my %external_manual = map { $_ => 1 } qw(openssl.1 pledge.2 unveil.2 rcctl.8);
my %element         = map { $_ => 1 } qw(
  a b blockquote br caption cite code dd div dl dt em h1 h2 h3 h4 hr i
  kbd li main mark nav ol p pre samp section small span strong sub sup
  table tbody td th thead tr ul var
);
my %plain_attr  = map { $_ => 1 } qw(class id role aria-label title);
my $manual_name = qr/ [a-zA-Z0-9_] [a-zA-Z0-9_.+-]* /x;
my $section_id  = qr/ (?: [1-9][0-9]* [a-zA-Z]* | [lnLN] ) /x;
my $filename    = qr/ $manual_name \. $section_id /x;
my $css         = <<'CSS';
body { margin: 1.5em; color: #171717; background: #fff;
       font: 100% Georgia, serif; }
a { color: #124780; } h1, h2, h3 { font-family: Arial, sans-serif; }
code, pre, kbd, samp { font-family: Consolas, "Courier New", monospace; }
pre { overflow: auto; padding: .6em; background: #f2f2f2; }
.head, .foot { width: 100%; font-size: 90%; }
.head-vol, .foot-date { padding: 0 1em; }
.head-rtitle, .foot-right { text-align: right; }
td, th { padding: .2em .4em; vertical-align: top; }
dt { font-weight: bold; } dd { margin-bottom: .5em; }
.Bd-indent { margin-left: 2em; }
.permalink { color: inherit; text-decoration: none; }
CSS

sub file_bytes {
    my ($path) = @_;
    open my $input, '<:raw', $path or die "Cannot open $path: $!\n";
    local $/ = undef;
    my $bytes = <$input> // '';
    close $input or die "Cannot close $path: $!\n";
    return $bytes;
}

sub write_file {
    my ( $path, $data ) = @_;
    open my $output, '>:raw', $path or die "Cannot write $path: $!\n";
    print {$output} $data or die "Cannot write $path: $!\n";
    close $output         or die "Cannot close $path: $!\n";
    return;
}

sub escape_html {
    my ($value) = @_;
    $value =~ s/ & /&amp;/gx;
    $value =~ s/ < /&lt;/gx;
    $value =~ s/ > /&gt;/gx;
    $value =~ s/ " /&quot;/gx;
    return $value;
}

sub capture {
    my ( $program, @args ) = @_;
    open my $pipe, '-|', $program, @args or die "Cannot run $program: $!\n";
    binmode $pipe;
    local $/ = undef;
    my $data = <$pipe> // '';
    close $pipe;
    return ( $?, $data );
}

sub inventory {
    my ($root) = @_;
    my %pages;
    File::Find::find(
        {
            no_chdir => 1,
            wanted   => sub {
                my $path = $File::Find::name;
                my $name = basename($path);
                return
                  unless $name =~
                  / \A ( $manual_name ) \. ( $section_id ) \z /x;
                my ( $title, $section ) = ( lc $1, lc $2 );
                return                             if -d $path && !-l $path;
                die "Invalid manual input $path\n" if -l $path || !-f $path;
                my $key = "$title.$section";
                die "Duplicate topic $key\n" if exists $pages{$key};
                my $source = file_bytes($path);
                $source =~ s/ \r\n /\n/gx;
                my @body = grep { /\S/x && !/ \A [.' ]* \\ " /x }
                  split /\n/x, $source;
                my ( $directory, $alias ) =
                    @body == 1
                  ? $body[0] =~
                  / \A \.so [ \t]+ man($section_id)\/($filename) [ \t]* \z /x
                  : ();

                if ( defined $alias ) {
                    die "Alias section mismatch in $path\n"
                      unless $alias =~ / \. \Q$directory\E \z /ix;
                    $alias = lc $alias;
                }
                elsif ( $source =~
                    / ^ [.' ] [ \t]* (?: so | mso | pso | nx ) \b /mx )
                {
                    die "Unsupported manual include in $path\n";
                }
                $pages{$key} = {
                    name    => $title,
                    section => $section,
                    source  => $source,
                    alias   => $alias,
                };
                return;
            }
        },
        $root,
    );
    die "No manuals found under $root\n" unless keys %pages;
    return \%pages;
}

sub alias_target {
    my ( $key, $pages ) = @_;
    my %visited;
    while ( defined $pages->{$key}{alias} ) {
        die "Manual alias cycle at $key\n" if $visited{$key}++;
        $key = $pages->{$key}{alias};
        die "Manual alias target $key is missing\n"
          unless exists $pages->{$key};
    }
    return $key;
}

sub safe_style {
    my ($value) = @_;
    my %property = map { $_ => 1 } qw(
      width margin-left padding-left text-align vertical-align white-space
      border-style border-top-style border-bottom-style border-left-style
      border-right-style
    );
    my %literal = map { $_ => 1 } qw(
      auto nowrap left right center top middle bottom double solid none
    );
    my $length = qr/ \A -?[0-9]+(?:\.[0-9]+)?(?: em | ex | px | % )? \z /x;
    pos($value) = 0;

    while ( $value =~ / \G \s* ([a-z-]+) \s* : \s* ([a-z0-9.%-]+) \s* ; /gcx ) {
        my ( $name, $operand ) = ( $1, $2 );
        return 0
          unless $property{$name}
          && ( $literal{$operand} || $operand =~ $length );
    }
    return substr( $value, pos($value) ) =~ / \A \s* \z /x;
}

sub safe_href {
    my ( $href, $kind, $pages, $fragments ) = @_;
    if ( $kind eq 'Xr' ) {
        my ($target) = $href =~ / \A manual-($filename)\.html \z /x;
        die "Malformed manual cross-reference: $href\n" unless defined $target;
        $target = lc $target;
        return "manual-$target.html" if exists $pages->{$target};
        return                       if exists $external_manual{$target};
        die "Missing cross-reference $target\n";
    }
    if ( $href =~ / \A \# (\S+) \z /x ) {
        push @{$fragments}, $1;
        return $href;
    }

    # Mandoc marks links to external resources by their roff macro.
    # Preserve visible link labels as text.
    return if $kind =~ / \A (?: Lk | Mt | UR | MT | RsR | RsU ) \z /x;
    die "Disallowed manual hyperlink: $href\n";
}

sub safe_tag {
    my ( $tag, $attrs, $pages, $anchors, $fragments ) = @_;
    die "Disallowed manual HTML element: $tag\n" unless $element{$tag};
    my %attr;
    pos($attrs) = 0;
    while ( $attrs =~ / \G \s+ ([a-z-]+) = " ([^"<>]*) " /gcx ) {
        my ( $key, $value ) = ( $1, $2 );
        die "Duplicate $key in <$tag>\n" if exists $attr{$key};
        $attr{$key} = $value;
    }
    my $rest = substr( $attrs, pos($attrs) // 0 );
    die "Malformed attributes in <$tag>\n"
      unless $rest =~ / \A \s* \/? \s* \z /x;
    if ( exists $attr{href} ) {
        die "Misplaced hyperlink in <$tag>\n" unless $tag eq 'a';
        my $href =
          safe_href( $attr{href}, $attr{class} // '', $pages, $fragments );
        if ( defined $href ) { $attr{href} = $href; }
        else                 { delete $attr{href}; }
    }
    for my $key ( keys %attr ) {
        next if $plain_attr{$key} || $key eq 'href';
        next if $key eq 'style' && safe_style( $attr{$key} );
        next
          if $key =~ / \A (?: border | colspan | rowspan | start ) \z /x
          && $attr{$key} =~ / \A [0-9]+ \z /x;
        die "Disallowed attribute $key in <$tag>\n";
    }
    if ( exists $attr{id} ) {
        die "Duplicate manual anchor $attr{id}\n"
          if exists $anchors->{ $attr{id} };
        $anchors->{ $attr{id} } = $tag;
    }

    # Older MSHTML engines lay out generic blocks consistently.
    $tag = 'div' if $tag =~ / \A (?: main | nav | section ) \z /x;
    return
      "<$tag" . join( '', map { qq{ $_="$attr{$_}"} } sort keys %attr ) . '>';
}

sub safe_fragment {
    my ( $data, $pages ) = @_;
    die "Invalid control character in mandoc output\n"
      if $data =~ / [\x00-\x08\x0b\x0c\x0e-\x1f\x7f] /x;
    my ( %anchors, @fragments, $output );
    $output = '';
    pos($data) = 0;
    while ( pos($data) < length($data) ) {
        if ( $data =~ / \G ( [^<>]+ ) /gcx ) {
            $output .= $1;
        }
        elsif ( $data =~ / \G < /gcx ) {
            die "Malformed mandoc HTML\n"
              unless $data =~ / \G (\/?)([a-z][a-z0-9]*)([^<>]*) > /gcx;
            my ( $closing, $tag, $attrs ) = ( $1, $2, $3 );
            if ($closing) {
                die "Disallowed closing tag $tag\n"
                  unless $element{$tag} && $attrs eq '';
                $tag = 'div' if $tag =~ / \A (?: main | nav | section ) \z /x;
                $output .= "</$tag>";
            }
            else {
                $output .=
                  safe_tag( $tag, $attrs, $pages, \%anchors, \@fragments );
            }
        }
        else {
            die "Unexpected text in mandoc HTML\n";
        }
    }
    for my $anchor (@fragments) {
        die "Missing internal anchor $anchor\n" unless exists $anchors{$anchor};
    }
    return ( $output, \%anchors );
}

sub label {
    my ($key) = @_;
    my ( $name, $section ) = $key =~ / \A ($manual_name) \. ($section_id) \z /x;
    return "$name($section)";
}

sub section_label {
    my ($section) = @_;
    my ($group)   = $section =~ / \A ( [0-9]+ | [ln] ) /x;
    return "Section $section - " . ( $section_name{$group} // 'Manual Pages' );
}

sub html_document {
    my ( $title, $body ) = @_;
    $title = escape_html($title);
    return <<"HTML";
<!DOCTYPE HTML>
<html lang="en">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<title>$title</title>
<link rel="stylesheet" type="text/css" href="manual.css">
</head>
<body>
$body
</body>
</html>
HTML
}

sub topic_link {
    my ($key) = @_;
    return qq{<a href="manual-$key.html">} . label($key) . '</a>';
}

sub sitemap_item {
    my ( $name, $target ) = @_;
    $name   = escape_html($name);
    $target = escape_html($target) if defined $target;
    my $item =
      qq{<li><object type="text/sitemap">\n<param name="Name" value="$name">\n};
    $item .= qq{<param name="Local" value="$target">\n} if defined $target;
    return $item . "</object>\n";
}

sub write_navigation {
    my ( $pages, $version ) = @_;
    my %section;
    for my $key ( sort keys %{$pages} ) {
        push @{ $section{ $pages->{$key}{section} } }, $key;
    }
    my $title = "openi2pd $version Manual";
    my $home =
      "<h1>$title</h1>\n<p>Command and administration reference.</p>\n";
    my $contents =
      "<ul>\n" . sitemap_item( 'Manual home', 'index.html' ) . "</li>\n";
    for my $sec ( sort { section_order( $a, $b ) } keys %section ) {
        my $name = section_label($sec);
        $home     .= "<h2>$name</h2>\n<ul>\n";
        $contents .= sitemap_item( $name, undef ) . "<ul>\n";
        for my $key ( @{ $section{$sec} } ) {
            $home .= '<li>' . topic_link($key) . "</li>\n";
            $contents .=
              sitemap_item( label($key), "manual-$key.html" ) . "</li>\n";
        }
        $contents .= "</ul>\n</li>\n";
        $home     .= "</ul>\n";
    }
    write_file( 'index.html', html_document( $title, $home ) );
    write_file( 'manual.hhc',
        "<html><body>\n$contents</ul>\n</body></html>\n" );
    write_file( 'manual.css', $css );
    return;
}

sub write_index {
    my ($pages) = @_;
    my @terms;
    for my $key ( sort keys %{$pages} ) {
        my $topic  = label($key);
        my $target = "manual-$key.html";
        push @terms, [ $topic, $target ];
        my $anchors = $pages->{$key}{anchors} // {};
        for my $anchor ( sort keys %{$anchors} ) {

            # Index headings and option names with simple fragment IDs.
            next unless $anchor =~ / \A [a-zA-Z0-9_.-]+ \z /x;
            next
              unless $anchors->{$anchor} =~ / \A h[1-4] \z /x
              || $anchor =~ / \A -[a-zA-Z0-9-]+ \z /x;
            my $term = $anchor;
            $term =~ tr/_/ / unless $term =~ / \A - /x;
            push @terms, [ "$term ($topic)", "$target#$anchor" ];
        }
    }
    my $index = "<html><body>\n<ul>\n";
    for my $entry ( sort { lc $a->[0] cmp lc $b->[0] || $a->[1] cmp $b->[1] }
        @terms )
    {
        $index .= sitemap_item( @{$entry} ) . "</li>\n";
    }
    write_file( 'manual.hhk', "$index</ul>\n</body></html>\n" );
    return;
}

sub section_order {
    my ( $section_a, $section_b ) = @_;
    my ($number_a) = $section_a =~ / \A ([0-9]+) /x;
    my ($number_b) = $section_b =~ / \A ([0-9]+) /x;
    return ( ( $number_a // 1_000 ) <=> ( $number_b // 1_000 ) )
      || ( $section_a cmp $section_b );
}

sub write_topics {
    my ( $pages, $mandoc ) = @_;
    for my $key ( sort keys %{$pages} ) {
        my $topic = label($key);
        my $body  = qq{<p><a href="index.html">Manual home</a></p>\n};
        if ( defined $pages->{$key}{alias} ) {
            $body .=
                "<h1>$topic</h1>\n<p>See "
              . topic_link( alias_target( $key, $pages ) )
              . ".</p>\n";
        }
        else {
            # A prefix also avoids reserved Windows device basenames.
            my $source = "source-$key";
            write_file( $source, $pages->{$key}{source} );
            my ( $status, $markup ) =
              capture( $mandoc, '-T', 'html', '-W', 'warning,stop',
                '-O', 'fragment,man=manual-%N.%S.html', $source, );
            die "mandoc failed for $key (wait status $status)\n" if $status;
            die "mandoc returned no content for $key\n" unless length $markup;
            my ( $clean, $anchors ) = safe_fragment( $markup, $pages );
            $body .= $clean;
            $pages->{$key}{anchors} = $anchors;
        }
        write_file( "manual-$key.html", html_document( $topic, $body ) );
    }
    return;
}

sub write_project {
    my ( $version, @files ) = @_;

    # Window flags include HHWIN_PROP_TAB_SEARCH (0x400).
    my $project = <<"HHP";
[OPTIONS]
Compatibility=1.1 or later
Compiled file=openi2pd.chm
Contents file=manual.hhc
Default topic=index.html
Default Window=Manual
Display compile progress=Yes
Error log file=manual.log
Full-text search=Yes
Index file=manual.hhk
Language=0x409 English (United States)
Title=openi2pd $version Manual

[WINDOWS]
Manual="openi2pd $version Manual","manual.hhc","manual.hhk","index.html","index.html",,,,,0x23520,240,0x104e,[0,0,900,690],,,,,,,0

[FILES]
HHP
    $project .= join '', map { "$_\n" } @files;
    $project =~ s/ \n /\r\n/gx;
    write_file( 'manual.hhp', $project );
    return;
}

sub verify_archive {
    my ( $sevenzip, @files ) = @_;
    my $archive = 'openi2pd.chm';
    my $bytes   = file_bytes($archive);
    verify_search_directory($bytes);
    my ( $status, $report ) =
      capture( $sevenzip, 'x', '-tchm', '-y', '-overified', $archive, @files, );
    die "Cannot unpack compiled help (wait status $status):\n$report\n"
      if $status;
    for my $member (@files) {
        my $unpacked = file_bytes( File::Spec->catfile( 'verified', $member ) );
        die "Compiled help member differs: $member\n"
          unless $unpacked eq file_bytes($member);
    }
    return;
}

sub verify_search_directory {
    my ($archive) = @_;
    my $length = length $archive;
    die "Invalid HTML Help file header\n"
      if $length < 0x60
      || substr( $archive, 0, 8 ) ne 'ITSF' . pack( 'V', 3 );

    # ITSF v3 stores the directory offset and size at bytes 72 and 80.
    # Inspect the uncompressed directory since 7-Zip hides system streams.
    my ( $start, $start_high, $size, $size_high ) =
      unpack( 'V4', substr( $archive, 72, 16 ) );
    die "Invalid HTML Help directory\n"
      if $start_high
      || $size_high
      || $start > $length
      || $size < 4
      || $size > $length - $start
      || substr( $archive, $start, 4 ) ne 'ITSP';
    my $search      = '/$FIftiMain';
    my $search_name = pack( 'C', length $search ) . $search;
    die "Compiled help has no full-text search stream\n"
      if index( substr( $archive, $start, $size ), $search_name ) < 0;
    return;
}

sub compile_manual {
    my ( $pages, $version, $output, $tools ) = @_;
    write_navigation( $pages, $version );
    write_topics( $pages, $tools->{mandoc} );
    write_index($pages);
    my @files = (
        'index.html', 'manual.css', map { "manual-$_.html" } sort keys %{$pages}
    );
    write_project( $version, @files );
    my ( $status, $report ) = capture( $tools->{hhc}, 'manual.hhp' );
    my $log         = -f 'manual.log' ? file_bytes('manual.log') : '';
    my $diagnostics = "$report\n$log";
    write_file( File::Spec->catfile( $output, 'manual.log' ), $diagnostics );

    # The Microsoft command-line compiler signals success with exit 1.
    die "HTML Help compilation failed (wait status $status):\n$diagnostics\n"
      if $status != ( 1 << 8 )
      || !-s 'openi2pd.chm'
      || $diagnostics =~ / \b HHC[0-9]+ \s* : \s* (?: warning | error ) \b /ix;
    verify_archive( $tools->{sevenzip}, @files );
    my $destination = File::Spec->catfile( $output, 'openi2pd.chm' );
    rename 'openi2pd.chm', $destination
      or die "Cannot publish verified manual to $destination: $!\n";
    return;
}

sub main {
    die
"Usage: windows-manual.pl MAN_ROOT VERSION OUTPUT_DIR MANDOC HHC SEVENZIP\n"
      unless @ARGV == 6;
    my ( $man_root, $version, $output, $mandoc, $hhc, $sevenzip ) = @ARGV;
    die "Invalid software version\n"
      unless $version =~ / \A [0-9][a-zA-Z0-9.+-]* \z /x;
    die "Missing manual directory $man_root\n" unless -d $man_root;
    $man_root = abs_path($man_root);
    $output   = File::Spec->rel2abs($output);
    my $original = getcwd();
    make_path($output);
    my $pages = inventory($man_root);
    my $work  = tempdir( 'manual-XXXXXX', DIR => $output, CLEANUP => 1 );
    chdir $work or die "Cannot enter $work: $!\n";
    my $finished = eval {
        compile_manual( $pages, $version, $output,
            { mandoc => $mandoc, hhc => $hhc, sevenzip => $sevenzip } );
        1;
    };
    my $error = $@;
    chdir $original or die "Cannot restore the working directory: $!\n";
    croak $error unless $finished;
    printf "Verified %d manual entries in openi2pd.chm\n",
      scalar keys %{$pages};
    return;
}

main();
