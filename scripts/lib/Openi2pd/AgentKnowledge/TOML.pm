# See the LICENSE file at the top of the project tree for copyright
# and license details.
#
# openi2pd: typed TOML reader for the agent-knowledge configuration.

package Openi2pd::AgentKnowledge::TOML;

use 5.016;
use strict;
use warnings;

# Keep types explicit: a quoted integer is not an integer setting, and
# a TOML boolean is not Perl's numeric scalar. No config value is eval'd.
sub parse {
    my ( $class, $text ) = @_;
    $text =~ s/\r\n/\n/g;
    my $self  = bless { text => $text, at => 0, explicit => {} }, $class;
    my $root  = { type => 'table', value => {} };
    my $table = $root;
    while (1) {
        $self->_space(1);
        last if $self->{at} >= length $text;
        if ( $self->_take('[') ) {
            $self->_error('array tables are not valid validator settings')
              if $self->_take('[');
            my @keys = $self->_keys;
            $self->_expect(']');
            my $identity = join "\0", @keys;
            $self->_error('duplicate table') if $self->{explicit}{$identity}++;
            $table = $self->_table( $root, \@keys, 0 );
            $self->_error('table already established by dotted keys')
              if $table->{dotted};
        }
        else {
            my @keys = $self->_keys;
            $self->_expect('=');
            my $value = $self->_value;
            $self->_assign( $table, \@keys, $value );
        }
        $self->_space(0);
        last if $self->{at} >= length $text;
        $self->_expect("\n");
    }
    return $root;
}

sub _error {
    my ( $self, $message ) = @_;
    my $line = 1 + ( substr( $self->{text}, 0, $self->{at} ) =~ tr/\n/\n/ );
    die "TOML line $line: $message\n";
}

sub _take {
    my ( $self, $literal ) = @_;
    return 0
      if substr( $self->{text}, $self->{at}, length $literal ) ne $literal;
    $self->{at} += length $literal;
    return 1;
}

sub _expect {
    my ( $self, $literal ) = @_;
    $self->_space(0)                     unless $literal eq "\n";
    $self->_error("expected '$literal'") unless $self->_take($literal);
    return;
}

sub _space {
    my ( $self, $newlines ) = @_;
    while ( $self->{at} < length $self->{text} ) {
        my $character = substr( $self->{text}, $self->{at}, 1 );
        if (   $character eq ' '
            || $character eq "\t"
            || ( $newlines && $character eq "\n" ) )
        {
            ++$self->{at};
        }
        elsif ( $character eq '#' ) {
            my $end     = index( $self->{text}, "\n", $self->{at} );
            my $comment = substr( $self->{text}, $self->{at},
                $end < 0
                ? length( $self->{text} ) - $self->{at}
                : $end - $self->{at} );
            $self->_error('control character in comment')
              if $comment =~ /[\x00-\x08\x0b-\x1f\x7f]/;
            $self->{at} = $end < 0 ? length $self->{text} : $end;
        }
        else { last; }
    }
    return;
}

sub _keys {
    my ($self) = @_;
    my @keys;
    while (1) {
        $self->_space(0);
        my $head = substr( $self->{text}, $self->{at}, 1 );
        if ( $head eq q{"} || $head eq q{'} ) {
            push @keys, $self->_string(0);
        }
        else {
            my $tail = substr( $self->{text}, $self->{at} );
            $self->_error('expected key') unless $tail =~ /\A([A-Za-z0-9_-]+)/;
            push @keys, $1;
            $self->{at} += length $1;
        }
        $self->_error('key path exceeds 64 components') if @keys > 64;
        $self->_space(0);
        last unless $self->_take('.');
    }
    return @keys;
}

sub _string {
    my ( $self, $allow_multiline ) = @_;
    my $quote = substr( $self->{text}, $self->{at}++, 1 );
    my $multi = $allow_multiline && $self->_take( $quote x 2 );
    $self->_take("\n") if $multi;
    my $value = '';
    while ( $self->{at} < length $self->{text} ) {
        if ( $multi && $self->_take( $quote x 3 ) ) {

            # Four/five terminal quotes encode one/two literal quotes.
            for ( 1 .. 2 ) { $value .= $quote if $self->_take($quote); }
            return $value;
        }
        my $char = substr( $self->{text}, $self->{at}++, 1 );
        return $value if !$multi && $char eq $quote;
        $self->_error('newline in single-line string')
          if !$multi && $char eq "\n";
        $self->_error('control character in string')
          if ord($char) < 32 && $char ne "\t" && !( $multi && $char eq "\n" );
        $self->_error('DEL in string') if ord($char) == 127;
        if ( $quote eq q{"} && $char eq '\\' ) {
            if ($multi) {
                my $tail = substr( $self->{text}, $self->{at} );
                if ( $tail =~ /\A[ \t]*\n[ \t\n]*/ ) {
                    $self->{at} += length $&;
                    next;
                }
            }
            $self->_error('unfinished escape')
              if $self->{at} >= length $self->{text};
            my $escape  = substr( $self->{text}, $self->{at}++, 1 );
            my %escapes = (
                b    => "\b",
                t    => "\t",
                n    => "\n",
                f    => "\f",
                r    => "\r",
                q{"} => q{"},
                '\\' => '\\'
            );
            if    ( exists $escapes{$escape} ) { $value .= $escapes{$escape}; }
            elsif ( $escape eq 'u' || $escape eq 'U' ) {
                my $digits = $escape eq 'u' ? 4 : 8;
                my $hex    = substr( $self->{text}, $self->{at}, $digits );
                $self->_error('invalid Unicode escape')
                  unless length($hex) == $digits && $hex =~ /\A[0-9a-fA-F]+\z/;
                $self->{at} += $digits;
                my $code = hex $hex;
                $self->_error('invalid Unicode scalar')
                  if $code > 0x10ffff || ( $code >= 0xd800 && $code <= 0xdfff );
                $value .= chr $code;
            }
            else { $self->_error('unknown string escape'); }
        }
        else { $value .= $char; }
    }
    $self->_error('unterminated string');
}

sub _value {
    my ($self) = @_;

    # Contract settings need shallow arrays/tables. Reject hostile nested
    # invalid configuration before exhausting the Perl call stack.
    local $self->{depth} = ( $self->{depth} // 0 ) + 1;
    $self->_error('value nesting exceeds 64 levels') if $self->{depth} > 64;
    $self->_space(0);
    my $head = substr( $self->{text}, $self->{at}, 1 );
    if ( $head eq q{"} || $head eq q{'} ) {
        return { type => 'string', value => $self->_string(1) };
    }
    if ( $self->_take('[') ) {
        my @values;
        $self->_space(1);
        until ( $self->_take(']') ) {
            push @values, $self->_value;
            $self->_space(1);
            last if $self->_take(']');
            $self->_expect(',');
            $self->_space(1);
        }
        return { type => 'array', value => \@values };
    }
    if ( $self->_take('{') ) {
        my $table = { type => 'table', value => {} };
        $self->_space(0);
        unless ( $self->_take('}') ) {
            while (1) {
                my @keys = $self->_keys;
                $self->_expect('=');
                $self->_assign( $table, \@keys, $self->_value );
                $self->_space(0);
                last if $self->_take('}');
                $self->_expect(',');
                $self->_space(0);
                $self->_error('trailing comma in inline table')
                  if substr( $self->{text}, $self->{at}, 1 ) eq '}';
            }
        }
        $self->_seal($table);
        return $table;
    }
    my $tail = substr( $self->{text}, $self->{at} );
    $self->_error('expected value') unless $tail =~ /\A([^\s\],}#]+)/;
    my $token = $1;
    $self->{at} += length $token;
    return { type => 'boolean', value => $token eq 'true' ? 1 : 0 }
      if $token eq 'true' || $token eq 'false';
    if ( $token =~ /\A[+-]?(?:0|[1-9](?:_?[0-9])*)\z/ ) {
        ( my $integer = $token ) =~ s/_//g;
        return { type => 'integer', value => _bounded_integer( $integer, 10 ) };
    }
    if ( $token =~ /\A0([xob])([0-9a-fA-F](?:_?[0-9a-fA-F])*)\z/ ) {
        my ( $base, $digits ) = ( $1, $2 );
        $self->_error('invalid base integer')
          if ( $base eq 'b' && $digits =~ /[^01_]/ )
          || ( $base eq 'o' && $digits =~ /[^0-7_]/ );
        $digits =~ s/_//g;
        my $integer =
          _bounded_integer( $digits, $base eq 'x' ? 16 : $base eq 'b' ? 2 : 8 );
        return { type => 'integer', value => $integer };
    }

    # These TOML types are syntactically valid but none is a valid
    # validator option. Preserve type rejection instead of coercing them.
    if ( $token =~
/\A[+-]?(?:inf|nan|(?:0|[1-9](?:_?[0-9])*)(?:(?:\.[0-9](?:_?[0-9])*)(?:[eE][+-]?[0-9](?:_?[0-9])*)?|[eE][+-]?[0-9](?:_?[0-9])*))\z/
      )
    {
        return { type => 'float', value => $token };
    }
    if ( $token =~
/\A\d{4}-\d\d-\d\d(?:[Tt]\d\d:\d\d:\d\d(?:\.\d+)?(?:[Zz]|[+-]\d\d:\d\d)?)?\z/
        || $token =~ /\A\d\d:\d\d:\d\d(?:\.\d+)?\z/ )
    {
        return { type => 'datetime', value => $token };
    }
    $self->_error('invalid value');
}

sub _table {
    my ( $self, $node, $keys, $dotted ) = @_;
    for my $key ( @{$keys} ) {
        $self->_error('cannot extend inline table') if $node->{sealed};
        $self->_error('key is not a table') unless $node->{type} eq 'table';
        $node->{value}{$key} //= { type => 'table', value => {} };
        $node = $node->{value}{$key};
        $self->_error('key is not a table') unless $node->{type} eq 'table';
        $node->{dotted} = 1 if $dotted;
    }
    $self->_error('cannot redefine inline table') if $node->{sealed};
    return $node;
}

sub _assign {
    my ( $self, $table, $keys, $value ) = @_;
    my @parents = @{$keys};
    my $last    = pop @parents;
    $table = $self->_table( $table, \@parents, 1 );
    $self->_error('duplicate key') if exists $table->{value}{$last};
    $table->{value}{$last} = $value;
    return;
}

# The only integer option has range 1..1000. Preserve every admissible
# value and saturate out-of-range magnitudes for schema rejection without
# native integer overflow, floating coercion or architecture warnings.
sub _bounded_integer {
    my ( $digits, $base ) = @_;
    my $negative = $digits =~ s/\A-//;
    $digits =~ s/\A\+//;
    my $value = 0;
    for my $digit ( split //, lc $digits ) {
        my $number = index( '0123456789abcdef', $digit );
        $value = $value * $base + $number;
        last if $value > 1000;
    }
    return $negative ? -$value : $value;
}

sub _seal {
    my ( $self, $node ) = @_;
    my @pending = ( [ $node, 0 ] );
    while (@pending) {
        my ( $current, $depth ) = @{ pop @pending };
        next unless $current->{type} eq 'table';
        $self->_error('inline table exceeds 64 levels') if $depth > 64;
        $current->{sealed} = 1;
        push @pending, map { [ $_, $depth + 1 ] } values %{ $current->{value} };
    }
    return;
}

1;

__END__

=head1 NAME

Openi2pd::AgentKnowledge::TOML - typed validator configuration reader

=head1 API

  my $tree = Openi2pd::AgentKnowledge::TOML->parse($unicode_text);

C<parse> accepts decoded Unicode and returns an owned tree of hashes.
Every node has C<type> and C<value>. Types are C<table>, C<array>,
C<string>, C<integer>, C<boolean>, C<float> and C<datetime>. Table values
are hashes of child nodes; array values are ordered child-node arrays.
No input view escapes the call and no global parser state is retained.
Invalid syntax throws a line-numbered exception; the CLI maps it to
AK005 and exit 2. Callers must apply the validator schema to the tree.

=head1 GRAMMAR AND LIMITS

The configuration contract accepts basic and literal strings, including
multiline forms, Unicode escapes, quoted/dotted keys, explicit/implicit
table headers, inline tables, booleans, base-prefixed or decimal integers
and arrays with comments or trailing commas. Duplicate keys/tables,
extension of inline tables, reopening dotted-key tables, invalid escapes
and unescaped control characters fail closed.

Values or dotted-key paths deeper than 64 levels are rejected before
stack exhaustion. Inline table sealing is iterative and also rejects
combined nesting beyond 64 levels. Accepted settings need only shallow
nesting; valid
strings and line counts have no additional parser cap. Array tables
cannot represent any accepted validator setting and are rejected.

Float and date/time tokens are represented only for schema rejection;
no validator option accepts them. Integers preserve every accepted
value in the sole integer option's range 1..1000. Larger magnitudes are
saturated outside that range to avoid native integer overflow on
32-bit systems; this tree is not a general-purpose numeric TOML API.
The reader never evaluates configuration or loads non-core modules.

=cut
