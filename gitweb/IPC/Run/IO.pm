package IPC::Run::IO;

=head1 NAME

IPC::Run::IO -- I/O channels for IPC::Run.

=head1 SYNOPSIS

B<NOT IMPLEMENTED YET ON Win32! Win32 does not allow select() on
normal file descriptors; IPC::RUN::IO needs to use IPC::Run::Win32Helper
to do this.>

   use IPC::Run qw( io );

   ## The sense of '>' and '<' is opposite of perl's open(),
   ## but agrees with IPC::Run.
   $io = io( "filename", '>',  \$recv );
   $io = io( "filename", 'r',  \$recv );

   ## Append to $recv:
   $io = io( "filename", '>>', \$recv );
   $io = io( "filename", 'ra', \$recv );

   $io = io( "filename", '<',  \$send );
   $io = io( "filename", 'w',  \$send );

   $io = io( "filename", '<<', \$send );
   $io = io( "filename", 'wa', \$send );

   ## Handles / IO objects that the caller opens:
   $io = io( \*HANDLE,   '<',  \$send );

   $f = IO::Handle->new( ... ); # Any subclass of IO::Handle
   $io = io( $f, '<', \$send );

   require IPC::Run::IO;
   $io = IPC::Run::IO->new( ... );

   ## Then run(), harness(), or start():
   run $io, ...;

   ## You can, of course, use io() or IPC::Run::IO->new() as an
   ## argument to run(), harness, or start():
   run io( ... );

=head1 DESCRIPTION

This class and module allows filehandles and filenames to be harnessed for
I/O when used IPC::Run, independent of anything else IPC::Run is doing
(except that errors & exceptions can affect all things that IPC::Run is
doing).

=head1 SUBCLASSING

INCOMPATIBLE CHANGE: due to the awkwardness introduced in ripping pseudohashes
out of Perl, this class I<no longer> uses the fields pragma.

=cut

## This class is also used internally by IPC::Run in a very intimate way,
## since this is a partial factoring of code from IPC::Run plus some code
## needed to do standalone channels.  This factoring process will continue
## at some point.  Don't know how far how fast.

use strict;
use Carp;
use Fcntl;
use Symbol;

use IPC::Run::Debug;
use IPC::Run qw( Win32_MODE );

use vars qw{$VERSION};

BEGIN {
    $VERSION = '0.96';
    if (Win32_MODE) {
        eval "use IPC::Run::Win32Helper; require IPC::Run::Win32IO; 1"
          or ( $@ && die )
          or die "$!";
    }
}

sub _empty($);
*_empty = \&IPC::Run::_empty;

=head1 SUBROUTINES

=over 4

=item new

I think it takes >> or << along with some other data.

TODO: Needs more thorough documentation. Patches welcome.

=cut

sub new {
    my $class = shift;
    $class = ref $class || $class;

    my ( $external, $type, $internal ) = ( shift, shift, pop );

    croak "$class: '$_' is not a valid I/O operator"
      unless $type =~ /^(?:<<?|>>?)$/;

    my IPC::Run::IO $self = $class->_new_internal( $type, undef, undef, $internal, undef, @_ );

    if ( !ref $external ) {
        $self->{FILENAME} = $external;
    }
    elsif ( ref eq 'GLOB' || UNIVERSAL::isa( $external, 'IO::Handle' ) ) {
        $self->{HANDLE}     = $external;
        $self->{DONT_CLOSE} = 1;
    }
    else {
        croak "$class: cannot accept " . ref($external) . " to do I/O with";
    }

    return $self;
}

## IPC::Run uses this ctor, since it preparses things and needs more
## smarts.
sub _new_internal {
    my $class = shift;
    $class = ref $class || $class;

    $class = "IPC::Run::Win32IO"
      if Win32_MODE && $class eq "IPC::Run::IO";

    my IPC::Run::IO $self;
    $self = bless {}, $class;

    my ( $type, $kfd, $pty_id, $internal, $binmode, @filters ) = @_;

    # Older perls (<=5.00503, at least) don't do list assign to
    # psuedo-hashes well.
    $self->{TYPE}   = $type;
    $self->{KFD}    = $kfd;
    $self->{PTY_ID} = $pty_id;
    $self->binmode($binmode);
    $self->{FILTERS} = [@filters];

    ## Add an adapter to the end of the filter chain (which is usually just the
    ## read/writer sub pushed by IPC::Run) to the DEST or SOURCE, if need be.
    if ( $self->op =~ />/ ) {
        croak "'$_' missing a destination" if _empty $internal;
        $self->{DEST} = $internal;
        if ( UNIVERSAL::isa( $self->{DEST}, 'CODE' ) ) {
            ## Put a filter on the end of the filter chain to pass the
            ## output on to the CODE ref.  For SCALAR refs, the last
            ## filter in the chain writes directly to the scalar itself.  See
            ## _init_filters().  For CODE refs, however, we need to adapt from
            ## the SCALAR to calling the CODE.
            unshift(
                @{ $self->{FILTERS} },
                sub {
                    my ($in_ref) = @_;

                    return IPC::Run::input_avail() && do {
                        $self->{DEST}->($$in_ref);
                        $$in_ref = '';
                        1;
                      }
                }
            );
        }
    }
    else {
        croak "'$_' missing a source" if _empty $internal;
        $self->{SOURCE} = $internal;
        if ( UNIVERSAL::isa( $internal, 'CODE' ) ) {
            push(
                @{ $self->{FILTERS} },
                sub {
                    my ( $in_ref, $out_ref ) = @_;
                    return 0 if length $$out_ref;

                    return undef
                      if $self->{SOURCE_EMPTY};

                    my $in = $internal->();
                    unless ( defined $in ) {
                        $self->{SOURCE_EMPTY} = 1;
                        return undef;
                    }
                    return 0 unless length $in;
                    $$out_ref = $in;

                    return 1;
                }
            );
        }
        elsif ( UNIVERSAL::isa( $internal, 'SCALAR' ) ) {
            push(
                @{ $self->{FILTERS} },
                sub {
                    my ( $in_ref, $out_ref ) = @_;
                    return 0 if length $$out_ref;

                    ## pump() clears auto_close_ins, finish() sets it.
                    return $self->{HARNESS}->{auto_close_ins} ? undef : 0
                      if IPC::Run::_empty ${ $self->{SOURCE} }
                      || $self->{SOURCE_EMPTY};

                    $$out_ref = $$internal;
                    eval { $$internal = '' }
                      if $self->{HARNESS}->{clear_ins};

                    $self->{SOURCE_EMPTY} = $self->{HARNESS}->{auto_close_ins};

                    return 1;
                }
            );
        }
    }

    return $self;
}

=item filename

Gets/sets the filename.  Returns the value after the name change, if
any.

=cut

sub filename {
    my IPC::Run::IO $self = shift;
    $self->{FILENAME} = shift if @_;
    return $self->{FILENAME};
}

=item init

Does initialization required before this can be run.  This includes open()ing
the file, if necessary, and clearing the destination scalar if necessary.

=cut

sub init {
    my IPC::Run::IO $self = shift;

    $self->{SOURCE_EMPTY} = 0;
    ${ $self->{DEST} } = ''
      if $self->mode =~ /r/ && ref $self->{DEST} eq 'SCALAR';

    $self->open if defined $self->filename;
    $self->{FD} = $self->fileno;

    if ( !$self->{FILTERS} ) {
        $self->{FBUFS} = undef;
    }
    else {
        @{ $self->{FBUFS} } = map {
            my $s = "";
            \$s;
        } ( @{ $self->{FILTERS} }, '' );

        $self->{FBUFS}->[0] = $self->{DEST}
          if $self->{DEST} && ref $self->{DEST} eq 'SCALAR';
        push @{ $self->{FBUFS} }, $self->{SOURCE};
    }

    return undef;
}

=item open

If a filename was passed in, opens it.  Determines if the handle is open
via fileno().  Throws an exception on error.

=cut

my %open_flags = (
    '>'  => O_RDONLY,
    '>>' => O_RDONLY,
    '<'  => O_WRONLY | O_CREAT | O_TRUNC,
    '<<' => O_WRONLY | O_CREAT | O_APPEND,
);

sub open {
    my IPC::Run::IO $self = shift;

    croak "IPC::Run::IO: Can't open() a file with no name"
      unless defined $self->{FILENAME};
    $self->{HANDLE} = gensym unless $self->{HANDLE};

    _debug "opening '", $self->filename, "' mode '", $self->mode, "'"
      if _debugging_data;
    sysopen(
        $self->{HANDLE},
        $self->filename,
        $open_flags{ $self->op },
    ) or croak "IPC::Run::IO: $! opening '$self->{FILENAME}', mode '" . $self->mode . "'";

    return undef;
}

=item open_pipe

If this is a redirection IO object, this opens the pipe in a platform
independent manner.

=cut

sub _do_open {
    my $self = shift;
    my ( $child_debug_fd, $parent_handle ) = @_;

    if ( $self->dir eq "<" ) {
        ( $self->{TFD}, $self->{FD} ) = IPC::Run::_pipe_nb;
        if ($parent_handle) {
            CORE::open $parent_handle, ">&=$self->{FD}"
              or croak "$! duping write end of pipe for caller";
        }
    }
    else {
        ( $self->{FD}, $self->{TFD} ) = IPC::Run::_pipe;
        if ($parent_handle) {
            CORE::open $parent_handle, "<&=$self->{FD}"
              or croak "$! duping read end of pipe for caller";
        }
    }
}

sub open_pipe {
    my IPC::Run::IO $self = shift;

    ## Hmmm, Maybe allow named pipes one day.  But until then...
    croak "IPC::Run::IO: Can't pipe() when a file name has been set"
      if defined $self->{FILENAME};

    $self->_do_open(@_);

    ## return ( child_fd, parent_fd )
    return $self->dir eq "<"
      ? ( $self->{TFD}, $self->{FD} )
      : ( $self->{FD}, $self->{TFD} );
}

sub _cleanup {    ## Called from Run.pm's _cleanup
    my $self = shift;
    undef $self->{FAKE_PIPE};
}

=item close

Closes the handle.  Throws an exception on failure.


=cut

sub close {
    my IPC::Run::IO $self = shift;

    if ( defined $self->{HANDLE} ) {
        close $self->{HANDLE}
          or croak(
            "IPC::Run::IO: $! closing "
              . (
                defined $self->{FILENAME}
                ? "'$self->{FILENAME}'"
                : "handle"
              )
          );
    }
    else {
        IPC::Run::_close( $self->{FD} );
    }

    $self->{FD} = undef;

    return undef;
}

=item fileno

Returns the fileno of the handle.  Throws an exception on failure.


=cut

sub fileno {
    my IPC::Run::IO $self = shift;

    my $fd = fileno $self->{HANDLE};
    croak(
        "IPC::Run::IO: $! "
          . (
            defined $self->{FILENAME}
            ? "'$self->{FILENAME}'"
            : "handle"
          )
    ) unless defined $fd;

    return $fd;
}

=item mode

Returns the operator in terms of 'r', 'w', and 'a'.  There is a state
'ra', unlike Perl's open(), which indicates that data read from the
handle or file will be appended to the output if the output is a scalar.
This is only meaningful if the output is a scalar, it has no effect if
the output is a subroutine.

The redirection operators can be a little confusing, so here's a reference
table:

   >      r      Read from handle in to process
   <      w      Write from process out to handle
   >>     ra     Read from handle in to process, appending it to existing
                 data if the destination is a scalar.
   <<     wa     Write from process out to handle, appending to existing
                 data if IPC::Run::IO opened a named file.

=cut

sub mode {
    my IPC::Run::IO $self = shift;

    croak "IPC::Run::IO: unexpected arguments for mode(): @_" if @_;

    ## TODO: Optimize this
    return ( $self->{TYPE} =~ /</ ? 'w' : 'r' ) . ( $self->{TYPE} =~ /<<|>>/ ? 'a' : '' );
}

=item op

Returns the operation: '<', '>', '<<', '>>'.  See L</mode> if you want
to spell these 'r', 'w', etc.

=cut

sub op {
    my IPC::Run::IO $self = shift;

    croak "IPC::Run::IO: unexpected arguments for op(): @_" if @_;

    return $self->{TYPE};
}

=item binmode

Sets/gets whether this pipe is in binmode or not.  No effect off of Win32
OSs, of course, and on Win32, no effect after the harness is start()ed.

=cut

sub binmode {
    my IPC::Run::IO $self = shift;

    $self->{BINMODE} = shift if @_;

    return $self->{BINMODE};
}

=item dir

Returns the first character of $self->op.  This is either "<" or ">".

=cut

sub dir {
    my IPC::Run::IO $self = shift;

    croak "IPC::Run::IO: unexpected arguments for dir(): @_" if @_;

    return substr $self->{TYPE}, 0, 1;
}

##
## Filter Scaffolding
##
#my $filter_op ;        ## The op running a filter chain right now
#my $filter_num;        ## Which filter is being run right now.

use vars (
    '$filter_op',    ## The op running a filter chain right now
    '$filter_num'    ## Which filter is being run right now.
);

sub _init_filters {
    my IPC::Run::IO $self = shift;

    confess "\$self not an IPC::Run::IO" unless UNIVERSAL::isa( $self, "IPC::Run::IO" );
    $self->{FBUFS} = [];

    $self->{FBUFS}->[0] = $self->{DEST}
      if $self->{DEST} && ref $self->{DEST} eq 'SCALAR';

    return unless $self->{FILTERS} && @{ $self->{FILTERS} };

    push @{ $self->{FBUFS} }, map {
        my $s = "";
        \$s;
    } ( @{ $self->{FILTERS} }, '' );

    push @{ $self->{FBUFS} }, $self->{SOURCE};
}

=item poll

TODO: Needs confirmation that this is correct. Was previously undocumented.

I believe this is polling the IO for new input and then returns undef if there will never be any more input, 0 if there is none now, but there might be in the future, and TRUE if more input was gotten.

=cut

sub poll {
    my IPC::Run::IO $self = shift;
    my ($harness) = @_;

    if ( defined $self->{FD} ) {
        my $d = $self->dir;
        if ( $d eq "<" ) {
            if ( vec $harness->{WOUT}, $self->{FD}, 1 ) {
                _debug_desc_fd( "filtering data to", $self )
                  if _debugging_details;
                return $self->_do_filters($harness);
            }
        }
        elsif ( $d eq ">" ) {
            if ( vec $harness->{ROUT}, $self->{FD}, 1 ) {
                _debug_desc_fd( "filtering data from", $self )
                  if _debugging_details;
                return $self->_do_filters($harness);
            }
        }
    }
    return 0;
}

sub _do_filters {
    my IPC::Run::IO $self = shift;

    ( $self->{HARNESS} ) = @_;

    my ( $saved_op, $saved_num ) = ( $IPC::Run::filter_op, $IPC::Run::filter_num );
    $IPC::Run::filter_op  = $self;
    $IPC::Run::filter_num = -1;
    my $redos = 0;
    my $r;
    {
        $@ = '';
        $r = eval { IPC::Run::get_more_input(); };

        # Detect Resource temporarily unavailable and re-try 200 times (2 seconds),  assuming select behaves (which it doesn't always? need ref)
        if ( ( $@ || '' ) =~ $IPC::Run::_EAGAIN && $redos++ < 200 ) {
            select( undef, undef, undef, 0.01 );
            redo;
        }
    }
    ( $IPC::Run::filter_op, $IPC::Run::filter_num ) = ( $saved_op, $saved_num );
    $self->{HARNESS} = undef;
    die "ack ", $@ if $@;
    return $r;
}

=back

=head1 AUTHOR

Barrie Slaymaker <barries@slaysys.com>

=head1 TODO

Implement bidirectionality.

=cut

1;
