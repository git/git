# Error.pm
#
# Copyright (c) 1997-8 Graham Barr <gbarr@ti.com>. All rights reserved.
# This program is free software; you can redistribute it and/or
# modify it under the same terms as Perl itself.
#
# Based on my original Error.pm, and Exceptions.pm by Peter Seibel
# <peter@weblogic.com> and adapted by Jesse Glick <jglick@sig.bsh.com>.
#
# but modified ***significantly***

package Error;

use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();

use vars qw($VERSION);
use 5.004;

$VERSION = "0.17025";

use overload (
	'""'	   =>	'stringify',
	'0+'	   =>	'value',
	'bool'     =>	sub { return 1; },
	'fallback' =>	1
);

$Error::Depth = 0;	# Depth to pass to caller()
$Error::Debug = 0;	# Generate verbose stack traces
@Error::STACK = ();	# Clause stack for try
$Error::THROWN = undef;	# last error thrown, a workaround until die $ref works

my $LAST;		# Last error created
my %ERROR;		# Last error associated with package

sub _throw_Error_Simple
{
    my $args = shift;
    return Error::Simple->new($args->{'text'});
}

$Error::ObjectifyCallback = \&_throw_Error_Simple;


# Exported subs are defined in Error::subs

use Scalar::Util ();

sub import {
    shift;
    my @tags = @_;
    local $Exporter::ExportLevel = $Exporter::ExportLevel + 1;

    @tags = grep {
       if( $_ eq ':warndie' ) {
          Error::WarnDie->import();
          0;
       }
       else {
          1;
       }
    } @tags;

    Error::subs->import(@tags);
}

# I really want to use last for the name of this method, but it is a keyword
# which prevent the syntax  last Error

sub prior {
    shift; # ignore

    return $LAST unless @_;

    my $pkg = shift;
    return exists $ERROR{$pkg} ? $ERROR{$pkg} : undef
	unless ref($pkg);

    my $obj = $pkg;
    my $err = undef;
    if($obj->isa('HASH')) {
	$err = $obj->{'__Error__'}
	    if exists $obj->{'__Error__'};
    }
    elsif($obj->isa('GLOB')) {
	$err = ${*$obj}{'__Error__'}
	    if exists ${*$obj}{'__Error__'};
    }

    $err;
}

sub flush {
    shift; #ignore

    unless (@_) {
       $LAST = undef;
       return;
    }

    my $pkg = shift;
    return unless ref($pkg);

    undef $ERROR{$pkg} if defined $ERROR{$pkg};
}

# Return as much information as possible about where the error
# happened. The -stacktrace element only exists if $Error::DEBUG
# was set when the error was created

sub stacktrace {
    my $self = shift;

    return $self->{'-stacktrace'}
	if exists $self->{'-stacktrace'};

    my $text = exists $self->{'-text'} ? $self->{'-text'} : "Died";

    $text .= sprintf(" at %s line %d.\n", $self->file, $self->line)
	unless($text =~ /\n$/s);

    $text;
}


sub associate {
    my $err = shift;
    my $obj = shift;

    return unless ref($obj);

    if($obj->isa('HASH')) {
	$obj->{'__Error__'} = $err;
    }
    elsif($obj->isa('GLOB')) {
	${*$obj}{'__Error__'} = $err;
    }
    $obj = ref($obj);
    $ERROR{ ref($obj) } = $err;

    return;
}


sub new {
    my $self = shift;
    my($pkg,$file,$line) = caller($Error::Depth);

    my $err = bless {
	'-package' => $pkg,
	'-file'    => $file,
	'-line'    => $line,
	@_
    }, $self;

    $err->associate($err->{'-object'})
	if(exists $err->{'-object'});

    # To always create a stacktrace would be very inefficient, so
    # we only do it if $Error::Debug is set

    if($Error::Debug) {
	require Carp;
	local $Carp::CarpLevel = $Error::Depth;
	my $text = defined($err->{'-text'}) ? $err->{'-text'} : "Error";
	my $trace = Carp::longmess($text);
	# Remove try calls from the trace
	$trace =~ s/(\n\s+\S+__ANON__[^\n]+)?\n\s+eval[^\n]+\n\s+Error::subs::try[^\n]+(?=\n)//sog;
	$trace =~ s/(\n\s+\S+__ANON__[^\n]+)?\n\s+eval[^\n]+\n\s+Error::subs::run_clauses[^\n]+\n\s+Error::subs::try[^\n]+(?=\n)//sog;
	$err->{'-stacktrace'} = $trace
    }

    $@ = $LAST = $ERROR{$pkg} = $err;
}

# Throw an error. this contains some very gory code.

sub throw {
    my $self = shift;
    local $Error::Depth = $Error::Depth + 1;

    # if we are not rethrow-ing then create the object to throw
    $self = $self->new(@_) unless ref($self);

    die $Error::THROWN = $self;
}

# syntactic sugar for
#
#    die with Error( ... );

sub with {
    my $self = shift;
    local $Error::Depth = $Error::Depth + 1;

    $self->new(@_);
}

# syntactic sugar for
#
#    record Error( ... ) and return;

sub record {
    my $self = shift;
    local $Error::Depth = $Error::Depth + 1;

    $self->new(@_);
}

# catch clause for
#
# try { ... } catch CLASS with { ... }

sub catch {
    my $pkg = shift;
    my $code = shift;
    my $clauses = shift || {};
    my $catch = $clauses->{'catch'} ||= [];

    unshift @$catch,  $pkg, $code;

    $clauses;
}

# Object query methods

sub object {
    my $self = shift;
    exists $self->{'-object'} ? $self->{'-object'} : undef;
}

sub file {
    my $self = shift;
    exists $self->{'-file'} ? $self->{'-file'} : undef;
}

sub line {
    my $self = shift;
    exists $self->{'-line'} ? $self->{'-line'} : undef;
}

sub text {
    my $self = shift;
    exists $self->{'-text'} ? $self->{'-text'} : undef;
}

# overload methods

sub stringify {
    my $self = shift;
    defined $self->{'-text'} ? $self->{'-text'} : "Died";
}

sub value {
    my $self = shift;
    exists $self->{'-value'} ? $self->{'-value'} : undef;
}

package Error::Simple;

use vars qw($VERSION);

$VERSION = "0.17025";

@Error::Simple::ISA = qw(Error);

sub new {
    my $self  = shift;
    my $text  = "" . shift;
    my $value = shift;
    my(@args) = ();

    local $Error::Depth = $Error::Depth + 1;

    @args = ( -file => $1, -line => $2)
	if($text =~ s/\s+at\s+(\S+)\s+line\s+(\d+)(?:,\s*<[^>]*>\s+line\s+\d+)?\.?\n?$//s);
    push(@args, '-value', 0 + $value)
	if defined($value);

    $self->SUPER::new(-text => $text, @args);
}

sub stringify {
    my $self = shift;
    my $text = $self->SUPER::stringify;
    $text .= sprintf(" at %s line %d.\n", $self->file, $self->line)
	unless($text =~ /\n$/s);
    $text;
}

##########################################################################
##########################################################################

# Inspired by code from Jesse Glick <jglick@sig.bsh.com> and
# Peter Seibel <peter@weblogic.com>

package Error::subs;

use Exporter ();
use vars qw(@EXPORT_OK @ISA %EXPORT_TAGS);

@EXPORT_OK   = qw(try with finally except otherwise);
%EXPORT_TAGS = (try => \@EXPORT_OK);

@ISA = qw(Exporter);

sub run_clauses ($$$\@) {
    my($clauses,$err,$wantarray,$result) = @_;
    my $code = undef;

    $err = $Error::ObjectifyCallback->({'text' =>$err}) unless ref($err);

    CATCH: {

	# catch
	my $catch;
	if(defined($catch = $clauses->{'catch'})) {
	    my $i = 0;

	    CATCHLOOP:
	    for( ; $i < @$catch ; $i += 2) {
		my $pkg = $catch->[$i];
		unless(defined $pkg) {
		    #except
		    splice(@$catch,$i,2,$catch->[$i+1]->($err));
		    $i -= 2;
		    next CATCHLOOP;
		}
		elsif(Scalar::Util::blessed($err) && $err->isa($pkg)) {
		    $code = $catch->[$i+1];
		    while(1) {
			my $more = 0;
			local($Error::THROWN, $@);
			my $ok = eval {
			    $@ = $err;
			    if($wantarray) {
				@{$result} = $code->($err,\$more);
			    }
			    elsif(defined($wantarray)) {
			        @{$result} = ();
				$result->[0] = $code->($err,\$more);
			    }
			    else {
				$code->($err,\$more);
			    }
			    1;
			};
			if( $ok ) {
			    next CATCHLOOP if $more;
			    undef $err;
			}
			else {
			    $err = $@ || $Error::THROWN;
				$err = $Error::ObjectifyCallback->({'text' =>$err})
					unless ref($err);
			}
			last CATCH;
		    };
		}
	    }
	}

	# otherwise
	my $owise;
	if(defined($owise = $clauses->{'otherwise'})) {
	    my $code = $clauses->{'otherwise'};
	    my $more = 0;
        local($Error::THROWN, $@);
	    my $ok = eval {
		$@ = $err;
		if($wantarray) {
		    @{$result} = $code->($err,\$more);
		}
		elsif(defined($wantarray)) {
		    @{$result} = ();
		    $result->[0] = $code->($err,\$more);
		}
		else {
		    $code->($err,\$more);
		}
		1;
	    };
	    if( $ok ) {
		undef $err;
	    }
	    else {
		$err = $@ || $Error::THROWN;

		$err = $Error::ObjectifyCallback->({'text' =>$err})
			unless ref($err);
	    }
	}
    }
    $err;
}

sub try (&;$) {
    my $try = shift;
    my $clauses = @_ ? shift : {};
    my $ok = 0;
    my $err = undef;
    my @result = ();

    unshift @Error::STACK, $clauses;

    my $wantarray = wantarray();

    do {
	local $Error::THROWN = undef;
	local $@ = undef;

	$ok = eval {
	    if($wantarray) {
		@result = $try->();
	    }
	    elsif(defined $wantarray) {
		$result[0] = $try->();
	    }
	    else {
		$try->();
	    }
	    1;
	};

	$err = $@ || $Error::THROWN
	    unless $ok;
    };

    shift @Error::STACK;

    $err = run_clauses($clauses,$err,wantarray,@result)
    unless($ok);

    $clauses->{'finally'}->()
	if(defined($clauses->{'finally'}));

    if (defined($err))
    {
        if (Scalar::Util::blessed($err) && $err->can('throw'))
        {
            throw $err;
        }
        else
        {
            die $err;
        }
    }

    wantarray ? @result : $result[0];
}

# Each clause adds a sub to the list of clauses. The finally clause is
# always the last, and the otherwise clause is always added just before
# the finally clause.
#
# All clauses, except the finally clause, add a sub which takes one argument
# this argument will be the error being thrown. The sub will return a code ref
# if that clause can handle that error, otherwise undef is returned.
#
# The otherwise clause adds a sub which unconditionally returns the users
# code reference, this is why it is forced to be last.
#
# The catch clause is defined in Error.pm, as the syntax causes it to
# be called as a method

sub with (&;$) {
    @_
}

sub finally (&) {
    my $code = shift;
    my $clauses = { 'finally' => $code };
    $clauses;
}

# The except clause is a block which returns a hashref or a list of
# key-value pairs, where the keys are the classes and the values are subs.

sub except (&;$) {
    my $code = shift;
    my $clauses = shift || {};
    my $catch = $clauses->{'catch'} ||= [];

    my $sub = sub {
	my $ref;
	my(@array) = $code->($_[0]);
	if(@array == 1 && ref($array[0])) {
	    $ref = $array[0];
	    $ref = [ %$ref ]
		if(UNIVERSAL::isa($ref,'HASH'));
	}
	else {
	    $ref = \@array;
	}
	@$ref
    };

    unshift @{$catch}, undef, $sub;

    $clauses;
}

sub otherwise (&;$) {
    my $code = shift;
    my $clauses = shift || {};

    if(exists $clauses->{'otherwise'}) {
	require Carp;
	Carp::croak("Multiple otherwise clauses");
    }

    $clauses->{'otherwise'} = $code;

    $clauses;
}

1;

package Error::WarnDie;

sub gen_callstack($)
{
    my ( $start ) = @_;

    require Carp;
    local $Carp::CarpLevel = $start;
    my $trace = Carp::longmess("");
    # Remove try calls from the trace
    $trace =~ s/(\n\s+\S+__ANON__[^\n]+)?\n\s+eval[^\n]+\n\s+Error::subs::try[^\n]+(?=\n)//sog;
    $trace =~ s/(\n\s+\S+__ANON__[^\n]+)?\n\s+eval[^\n]+\n\s+Error::subs::run_clauses[^\n]+\n\s+Error::subs::try[^\n]+(?=\n)//sog;
    my @callstack = split( m/\n/, $trace );
    return @callstack;
}

my $old_DIE;
my $old_WARN;

sub DEATH
{
    my ( $e ) = @_;

    local $SIG{__DIE__} = $old_DIE if( defined $old_DIE );

    die @_ if $^S;

    my ( $etype, $message, $location, @callstack );
    if ( ref($e) && $e->isa( "Error" ) ) {
        $etype = "exception of type " . ref( $e );
        $message = $e->text;
        $location = $e->file . ":" . $e->line;
        @callstack = split( m/\n/, $e->stacktrace );
    }
    else {
        # Don't apply subsequent layer of message formatting
        die $e if( $e =~ m/^\nUnhandled perl error caught at toplevel:\n\n/ );
        $etype = "perl error";
        my $stackdepth = 0;
        while( caller( $stackdepth ) =~ m/^Error(?:$|::)/ ) {
            $stackdepth++
        }

        @callstack = gen_callstack( $stackdepth + 1 );

        $message = "$e";
        chomp $message;

        if ( $message =~ s/ at (.*?) line (\d+)\.$// ) {
            $location = $1 . ":" . $2;
        }
        else {
            my @caller = caller( $stackdepth );
            $location = $caller[1] . ":" . $caller[2];
        }
    }

    shift @callstack;
    # Do it this way in case there are no elements; we don't print a spurious \n
    my $callstack = join( "", map { "$_\n"} @callstack );

    die "\nUnhandled $etype caught at toplevel:\n\n  $message\n\nThrown from: $location\n\nFull stack trace:\n\n$callstack\n";
}

sub TAXES
{
    my ( $message ) = @_;

    local $SIG{__WARN__} = $old_WARN if( defined $old_WARN );

    $message =~ s/ at .*? line \d+\.$//;
    chomp $message;

    my @callstack = gen_callstack( 1 );
    my $location = shift @callstack;

    # $location already starts in a leading space
    $message .= $location;

    # Do it this way in case there are no elements; we don't print a spurious \n
    my $callstack = join( "", map { "$_\n"} @callstack );

    warn "$message:\n$callstack";
}

sub import
{
    $old_DIE  = $SIG{__DIE__};
    $old_WARN = $SIG{__WARN__};

    $SIG{__DIE__}  = \&DEATH;
    $SIG{__WARN__} = \&TAXES;
}

1;

__END__

=head1 NAME

Error - Error/exception handling in an OO-ish way

=head1 WARNING

Using the "Error" module is B<no longer recommended> due to the black-magical
nature of its syntactic sugar, which often tends to break. Its maintainers
have stopped actively writing code that uses it, and discourage people
from doing so. See the "SEE ALSO" section below for better recommendations.

=head1 SYNOPSIS

    use Error qw(:try);

    throw Error::Simple( "A simple error");

    sub xyz {
        ...
	record Error::Simple("A simple error")
	    and return;
    }

    unlink($file) or throw Error::Simple("$file: $!",$!);

    try {
	do_some_stuff();
	die "error!" if $condition;
	throw Error::Simple "Oops!" if $other_condition;
    }
    catch Error::IO with {
	my $E = shift;
	print STDERR "File ", $E->{'-file'}, " had a problem\n";
    }
    except {
	my $E = shift;
	my $general_handler=sub {send_message $E->{-description}};
	return {
	    UserException1 => $general_handler,
	    UserException2 => $general_handler
	};
    }
    otherwise {
	print STDERR "Well I don't know what to say\n";
    }
    finally {
	close_the_garage_door_already(); # Should be reliable
    }; # Don't forget the trailing ; or you might be surprised

=head1 DESCRIPTION

The C<Error> package provides two interfaces. Firstly C<Error> provides
a procedural interface to exception handling. Secondly C<Error> is a
base class for errors/exceptions that can either be thrown, for
subsequent catch, or can simply be recorded.

Errors in the class C<Error> should not be thrown directly, but the
user should throw errors from a sub-class of C<Error>.

=head1 PROCEDURAL INTERFACE

C<Error> exports subroutines to perform exception handling. These will
be exported if the C<:try> tag is used in the C<use> line.

=over 4

=item try BLOCK CLAUSES

C<try> is the main subroutine called by the user. All other subroutines
exported are clauses to the try subroutine.

The BLOCK will be evaluated and, if no error is throw, try will return
the result of the block.

C<CLAUSES> are the subroutines below, which describe what to do in the
event of an error being thrown within BLOCK.

=item catch CLASS with BLOCK

This clauses will cause all errors that satisfy C<$err-E<gt>isa(CLASS)>
to be caught and handled by evaluating C<BLOCK>.

C<BLOCK> will be passed two arguments. The first will be the error
being thrown. The second is a reference to a scalar variable. If this
variable is set by the catch block then, on return from the catch
block, try will continue processing as if the catch block was never
found. The error will also be available in C<$@>.

To propagate the error the catch block may call C<$err-E<gt>throw>

If the scalar reference by the second argument is not set, and the
error is not thrown. Then the current try block will return with the
result from the catch block.

=item except BLOCK

When C<try> is looking for a handler, if an except clause is found
C<BLOCK> is evaluated. The return value from this block should be a
HASHREF or a list of key-value pairs, where the keys are class names
and the values are CODE references for the handler of errors of that
type.

=item otherwise BLOCK

Catch any error by executing the code in C<BLOCK>

When evaluated C<BLOCK> will be passed one argument, which will be the
error being processed. The error will also be available in C<$@>.

Only one otherwise block may be specified per try block

=item finally BLOCK

Execute the code in C<BLOCK> either after the code in the try block has
successfully completed, or if the try block throws an error then
C<BLOCK> will be executed after the handler has completed.

If the handler throws an error then the error will be caught, the
finally block will be executed and the error will be re-thrown.

Only one finally block may be specified per try block

=back

=head1 COMPATIBILITY

L<Moose> exports a keyword called C<with> which clashes with Error's. This
example returns a prototype mismatch error:

    package MyTest;

    use warnings;
    use Moose;
    use Error qw(:try);

(Thanks to C<maik.hentsche@amd.com> for the report.).

=head1 CLASS INTERFACE

=head2 CONSTRUCTORS

The C<Error> object is implemented as a HASH. This HASH is initialized
with the arguments that are passed to it's constructor. The elements
that are used by, or are retrievable by the C<Error> class are listed
below, other classes may add to these.

	-file
	-line
	-text
	-value
	-object

If C<-file> or C<-line> are not specified in the constructor arguments
then these will be initialized with the file name and line number where
the constructor was called from.

If the error is associated with an object then the object should be
passed as the C<-object> argument. This will allow the C<Error> package
to associate the error with the object.

The C<Error> package remembers the last error created, and also the
last error associated with a package. This could either be the last
error created by a sub in that package, or the last error which passed
an object blessed into that package as the C<-object> argument.

=over 4

=item Error->new()

See the Error::Simple documentation.

=item throw ( [ ARGS ] )

Create a new C<Error> object and throw an error, which will be caught
by a surrounding C<try> block, if there is one. Otherwise it will cause
the program to exit.

C<throw> may also be called on an existing error to re-throw it.

=item with ( [ ARGS ] )

Create a new C<Error> object and returns it. This is defined for
syntactic sugar, eg

    die with Some::Error ( ... );

=item record ( [ ARGS ] )

Create a new C<Error> object and returns it. This is defined for
syntactic sugar, eg

    record Some::Error ( ... )
	and return;

=back

=head2 STATIC METHODS

=over 4

=item prior ( [ PACKAGE ] )

Return the last error created, or the last error associated with
C<PACKAGE>

=item flush ( [ PACKAGE ] )

Flush the last error created, or the last error associated with
C<PACKAGE>.It is necessary to clear the error stack before exiting the
package or uncaught errors generated using C<record> will be reported.

     $Error->flush;

=cut

=back

=head2 OBJECT METHODS

=over 4

=item stacktrace

If the variable C<$Error::Debug> was non-zero when the error was
created, then C<stacktrace> returns a string created by calling
C<Carp::longmess>. If the variable was zero the C<stacktrace> returns
the text of the error appended with the filename and line number of
where the error was created, providing the text does not end with a
newline.

=item object

The object this error was associated with

=item file

The file where the constructor of this error was called from

=item line

The line where the constructor of this error was called from

=item text

The text of the error

=item $err->associate($obj)

Associates an error with an object to allow error propagation. I.e:

    $ber->encode(...) or
        return Error->prior($ber)->associate($ldap);

=back

=head2 OVERLOAD METHODS

=over 4

=item stringify

A method that converts the object into a string. This method may simply
return the same as the C<text> method, or it may append more
information. For example the file name and line number.

By default this method returns the C<-text> argument that was passed to
the constructor, or the string C<"Died"> if none was given.

=item value

A method that will return a value that can be associated with the
error. For example if an error was created due to the failure of a
system call, then this may return the numeric value of C<$!> at the
time.

By default this method returns the C<-value> argument that was passed
to the constructor.

=back

=head1 PRE-DEFINED ERROR CLASSES

=head2 Error::Simple

This class can be used to hold simple error strings and values. It's
constructor takes two arguments. The first is a text value, the second
is a numeric value. These values are what will be returned by the
overload methods.

If the text value ends with C<at file line 1> as $@ strings do, then
this information will be used to set the C<-file> and C<-line> arguments
of the error object.

This class is used internally if an eval'd block die's with an error
that is a plain string. (Unless C<$Error::ObjectifyCallback> is modified)


=head1 $Error::ObjectifyCallback

This variable holds a reference to a subroutine that converts errors that
are plain strings to objects. It is used by Error.pm to convert textual
errors to objects, and can be overridden by the user.

It accepts a single argument which is a hash reference to named parameters.
Currently the only named parameter passed is C<'text'> which is the text
of the error, but others may be available in the future.

For example the following code will cause Error.pm to throw objects of the
class MyError::Bar by default:

    sub throw_MyError_Bar
    {
        my $args = shift;
        my $err = MyError::Bar->new();
        $err->{'MyBarText'} = $args->{'text'};
        return $err;
    }

    {
        local $Error::ObjectifyCallback = \&throw_MyError_Bar;

        # Error handling here.
    }

=cut

=head1 MESSAGE HANDLERS

C<Error> also provides handlers to extend the output of the C<warn()> perl
function, and to handle the printing of a thrown C<Error> that is not caught
or otherwise handled. These are not installed by default, but are requested
using the C<:warndie> tag in the C<use> line.

 use Error qw( :warndie );

These new error handlers are installed in C<$SIG{__WARN__}> and
C<$SIG{__DIE__}>. If these handlers are already defined when the tag is
imported, the old values are stored, and used during the new code. Thus, to
arrange for custom handling of warnings and errors, you will need to perform
something like the following:

 BEGIN {
   $SIG{__WARN__} = sub {
     print STDERR "My special warning handler: $_[0]"
   };
 }

 use Error qw( :warndie );

Note that setting C<$SIG{__WARN__}> after the C<:warndie> tag has been
imported will overwrite the handler that C<Error> provides. If this cannot be
avoided, then the tag can be explicitly C<import>ed later

 use Error;

 $SIG{__WARN__} = ...;

 import Error qw( :warndie );

=head2 EXAMPLE

The C<__DIE__> handler turns messages such as

 Can't call method "foo" on an undefined value at examples/warndie.pl line 16.

into

 Unhandled perl error caught at toplevel:

   Can't call method "foo" on an undefined value

 Thrown from: examples/warndie.pl:16

 Full stack trace:

         main::inner('undef') called at examples/warndie.pl line 20
         main::outer('undef') called at examples/warndie.pl line 23

=cut

=head1 SEE ALSO

See L<Exception::Class> for a different module providing Object-Oriented
exception handling, along with a convenient syntax for declaring hierarchies
for them. It doesn't provide Error's syntactic sugar of C<try { ... }>,
C<catch { ... }>, etc. which may be a good thing or a bad thing based
on what you want. (Because Error's syntactic sugar tends to break.)

L<Error::Exception> aims to combine L<Error> and L<Exception::Class>
"with correct stringification".

L<TryCatch> and L<Try::Tiny> are similar in concept to Error.pm only providing
a syntax that hopefully breaks less.

=head1 KNOWN BUGS

None, but that does not mean there are not any.

=head1 AUTHORS

Graham Barr <gbarr@pobox.com>

The code that inspired me to write this was originally written by
Peter Seibel <peter@weblogic.com> and adapted by Jesse Glick
<jglick@sig.bsh.com>.

C<:warndie> handlers added by Paul Evans <leonerd@leonerd.org.uk>

=head1 MAINTAINER

Shlomi Fish, L<https://www.shlomifish.org/> .

=head1 PAST MAINTAINERS

Arun Kumar U <u_arunkumar@yahoo.com>

=head1 COPYRIGHT

Copyright (c) 1997-8  Graham Barr. All rights reserved.
This program is free software; you can redistribute it and/or modify it
under the same terms as Perl itself.

=cut
