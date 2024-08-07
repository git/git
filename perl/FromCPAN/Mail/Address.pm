# Copyrights 1995-2018 by [Mark Overmeer].
#  For other contributors see ChangeLog.
# See the manual pages for details on the licensing terms.
# Pod stripped from pm file by OODoc 2.02.
# This code is part of the bundle MailTools.  Meta-POD processed with
# OODoc into POD and HTML manual-pages.  See README.md for Copyright.
# Licensed under the same terms as Perl itself.

package Mail::Address;
use vars '$VERSION';
$VERSION = '2.20';

use strict;

use Carp;

# use locale;   removed in version 1.78, because it causes taint problems

sub Version { our $VERSION }



# given a comment, attempt to extract a person's name
sub _extract_name
{   # This function can be called as method as well
    my $self = @_ && ref $_[0] ? shift : undef;

    local $_ = shift
        or return '';

    # Using encodings, too hard. See Mail::Message::Field::Full.
    return '' if m/\=\?.*?\?\=/;

    # trim whitespace
    s/^\s+//;
    s/\s+$//;
    s/\s+/ /;

    # Disregard numeric names (e.g. 123456.1234@compuserve.com)
    return "" if /^[\d ]+$/;

    s/^\((.*)\)$/$1/; # remove outermost parenthesis
    s/^"(.*)"$/$1/;   # remove outer quotation marks
    s/\(.*?\)//g;     # remove minimal embedded comments
    s/\\//g;          # remove all escapes
    s/^"(.*)"$/$1/;   # remove internal quotation marks
    s/^([^\s]+) ?, ?(.*)$/$2 $1/; # reverse "Last, First M." if applicable
    s/,.*//;

    # Change casing only when the name contains only upper or only
    # lower cased characters.
    unless( m/[A-Z]/ && m/[a-z]/ )
    {   # Set the case of the name to first char upper rest lower
        s/\b(\w+)/\L\u$1/igo;  # Upcase first letter on name
        s/\bMc(\w)/Mc\u$1/igo; # Scottish names such as 'McLeod'
        s/\bo'(\w)/O'\u$1/igo; # Irish names such as 'O'Malley, O'Reilly'
        s/\b(x*(ix)?v*(iv)?i*)\b/\U$1/igo; # Roman numerals, eg 'Level III Support'
    }

    # some cleanup
    s/\[[^\]]*\]//g;
    s/(^[\s'"]+|[\s'"]+$)//g;
    s/\s{2,}/ /g;

    $_;
}

sub _tokenise
{   local $_ = join ',', @_;
    my (@words,$snippet,$field);

    s/\A\s+//;
    s/[\r\n]+/ /g;

    while ($_ ne '')
    {   $field = '';
        if(s/^\s*\(/(/ )    # (...)
        {   my $depth = 0;

     PAREN: while(s/^(\(([^\(\)\\]|\\.)*)//)
            {   $field .= $1;
                $depth++;
                while(s/^(([^\(\)\\]|\\.)*\)\s*)//)
                {   $field .= $1;
                    last PAREN unless --$depth;
	            $field .= $1 if s/^(([^\(\)\\]|\\.)+)//;
                }
            }

            carp "Unmatched () '$field' '$_'"
                if $depth;

            $field =~ s/\s+\Z//;
            push @words, $field;

            next;
        }

        if( s/^("(?:[^"\\]+|\\.)*")\s*//       # "..."
         || s/^(\[(?:[^\]\\]+|\\.)*\])\s*//    # [...]
         || s/^([^\s()<>\@,;:\\".[\]]+)\s*//
         || s/^([()<>\@,;:\\".[\]])\s*//
          )
        {   push @words, $1;
            next;
        }

        croak "Unrecognised line: $_";
    }

    push @words, ",";
    \@words;
}

sub _find_next
{   my ($idx, $tokens, $len) = @_;

    while($idx < $len)
    {   my $c = $tokens->[$idx];
        return $c if $c eq ',' || $c eq ';' || $c eq '<';
        $idx++;
    }

    "";
}

sub _complete
{   my ($class, $phrase, $address, $comment) = @_;

    @$phrase || @$comment || @$address
       or return undef;

    my $o = $class->new(join(" ",@$phrase), join("",@$address), join(" ",@$comment));
    @$phrase = @$address = @$comment = ();
    $o;
}

#------------

sub new(@)
{   my $class = shift;
    bless [@_], $class;
}


sub parse(@)
{   my $class = shift;
    my @line  = grep {defined} @_;
    my $line  = join '', @line;

    my (@phrase, @comment, @address, @objs);
    my ($depth, $idx) = (0, 0);

    my $tokens  = _tokenise @line;
    my $len     = @$tokens;
    my $next    = _find_next $idx, $tokens, $len;

    local $_;
    for(my $idx = 0; $idx < $len; $idx++)
    {   $_ = $tokens->[$idx];

        if(substr($_,0,1) eq '(') { push @comment, $_ }
        elsif($_ eq '<')    { $depth++ }
        elsif($_ eq '>')    { $depth-- if $depth }
        elsif($_ eq ',' || $_ eq ';')
        {   warn "Unmatched '<>' in $line" if $depth;
            my $o = $class->_complete(\@phrase, \@address, \@comment);
            push @objs, $o if defined $o;
            $depth = 0;
            $next = _find_next $idx+1, $tokens, $len;
        }
        elsif($depth)       { push @address, $_ }
        elsif($next eq '<') { push @phrase,  $_ }
        elsif( /^[.\@:;]$/ || !@address || $address[-1] =~ /^[.\@:;]$/ )
        {   push @address, $_ }
        else
        {   warn "Unmatched '<>' in $line" if $depth;
            my $o = $class->_complete(\@phrase, \@address, \@comment);
            push @objs, $o if defined $o;
            $depth = 0;
            push @address, $_;
        }
    }
    @objs;
}

#------------

sub phrase  { shift->set_or_get(0, @_) }
sub address { shift->set_or_get(1, @_) }
sub comment { shift->set_or_get(2, @_) }

sub set_or_get($)
{   my ($self, $i) = (shift, shift);
    @_ or return $self->[$i];

    my $val = $self->[$i];
    $self->[$i] = shift if @_;
    $val;
}


my $atext = '[\-\w !#$%&\'*+/=?^`{|}~]';
sub format
{   my @addrs;

    foreach (@_)
    {   my ($phrase, $email, $comment) = @$_;
        my @addr;

        if(defined $phrase && length $phrase)
        {   push @addr
              , $phrase =~ /^(?:\s*$atext\s*)+$/o ? $phrase
              : $phrase =~ /(?<!\\)"/             ? $phrase
              :                                    qq("$phrase");

            push @addr, "<$email>"
                if defined $email && length $email;
        }
        elsif(defined $email && length $email)
        {   push @addr, $email;
        }

        if(defined $comment && $comment =~ /\S/)
        {   $comment =~ s/^\s*\(?/(/;
            $comment =~ s/\)?\s*$/)/;
        }

        push @addr, $comment
            if defined $comment && length $comment;

        push @addrs, join(" ", @addr)
            if @addr;
    }

    join ", ", @addrs;
}

#------------

sub name
{   my $self   = shift;
    my $phrase = $self->phrase;
    my $addr   = $self->address;

    $phrase    = $self->comment
        unless defined $phrase && length $phrase;

    my $name   = $self->_extract_name($phrase);

    # first.last@domain address
    if($name eq '' && $addr =~ /([^\%\.\@_]+([\._][^\%\.\@_]+)+)[\@\%]/)
    {   ($name  = $1) =~ s/[\._]+/ /g;
	$name   = _extract_name $name;
    }

    if($name eq '' && $addr =~ m#/g=#i)    # X400 style address
    {   my ($f) = $addr =~ m#g=([^/]*)#i;
	my ($l) = $addr =~ m#s=([^/]*)#i;
	$name   = _extract_name "$f $l";
    }

    length $name ? $name : undef;
}


sub host
{   my $addr = shift->address || '';
    my $i    = rindex $addr, '@';
    $i >= 0 ? substr($addr, $i+1) : undef;
}


sub user
{   my $addr = shift->address || '';
    my $i    = rindex $addr, '@';
    $i >= 0 ? substr($addr,0,$i) : $addr;
}

1;
