package IPC::Run::Debug;

=pod

=head1 NAME

IPC::Run::Debug - debugging routines for IPC::Run

=head1 SYNOPSIS

   ##
   ## Environment variable usage
   ##
   ## To force debugging off and shave a bit of CPU and memory
   ## by compile-time optimizing away all debugging code in IPC::Run
   ## (debug => ...) options to IPC::Run will be ignored.
   export IPCRUNDEBUG=none

   ## To force debugging on (levels are from 0..10)
   export IPCRUNDEBUG=basic

   ## Leave unset or set to "" to compile in debugging support and
   ## allow runtime control of it using the debug option.

=head1 DESCRIPTION

Controls IPC::Run debugging.  Debugging levels are now set by using words,
but the numbers shown are still supported for backwards compatibility:

   0  none         disabled (special, see below)
   1  basic        what's running
   2  data         what's being sent/received
   3  details      what's going on in more detail
   4  gory         way too much detail for most uses
   10 all          use this when submitting bug reports
      noopts       optimizations forbidden due to inherited STDIN

The C<none> level is special when the environment variable IPCRUNDEBUG
is set to this the first time IPC::Run::Debug is loaded: it prevents
the debugging code from being compiled in to the remaining IPC::Run modules,
saving a bit of cpu.

To do this in a script, here's a way that allows it to be overridden:

   BEGIN {
      unless ( defined $ENV{IPCRUNDEBUG} ) {
	 eval 'local $ENV{IPCRUNDEBUG} = "none"; require IPC::Run::Debug"'
	    or die $@;
      }
   }

This should force IPC::Run to not be debuggable unless somebody sets
the IPCRUNDEBUG flag; modify this formula to grep @ARGV if need be:

   BEGIN {
      unless ( grep /^--debug/, @ARGV ) {
	 eval 'local $ENV{IPCRUNDEBUG} = "none"; require IPC::Run::Debug"'
	 or die $@;
   }

Both of those are untested.

=cut

## We use @EXPORT for the end user's convenience: there's only one function
## exported, it's homonymous with the module, it's an unusual name, and
## it can be suppressed by "use IPC::Run ();".

use strict;
use Exporter;
use vars qw{$VERSION @ISA @EXPORT @EXPORT_OK %EXPORT_TAGS};

BEGIN {
    $VERSION = '0.96';
    @ISA     = qw( Exporter );
    @EXPORT  = qw(
      _debug
      _debug_desc_fd
      _debugging
      _debugging_data
      _debugging_details
      _debugging_gory_details
      _debugging_not_optimized
      _set_child_debug_name
    );

    @EXPORT_OK = qw(
      _debug_init
      _debugging_level
      _map_fds
    );
    %EXPORT_TAGS = (
        default => \@EXPORT,
        all => [ @EXPORT, @EXPORT_OK ],
    );
}

my $disable_debugging = defined $ENV{IPCRUNDEBUG}
  && ( !$ENV{IPCRUNDEBUG}
    || lc $ENV{IPCRUNDEBUG} eq "none" );

eval( $disable_debugging ? <<'STUBS' : <<'SUBS' ) or die $@;
sub _map_fds()                 { "" }
sub _debug                     {}
sub _debug_desc_fd             {}
sub _debug_init                {}
sub _set_child_debug_name      {}
sub _debugging()               { 0 }
sub _debugging_level()         { 0 }
sub _debugging_data()          { 0 }
sub _debugging_details()       { 0 }
sub _debugging_gory_details()  { 0 }
sub _debugging_not_optimized() { 0 }

1;
STUBS

use POSIX ();

sub _map_fds {
   my $map = '';
   my $digit = 0;
   my $in_use;
   my $dummy;
   for my $fd (0..63) {
      ## I'd like a quicker way (less user, cpu & especially sys and kernel
      ## calls) to detect open file descriptors.  Let me know...
      ## Hmmm, could do a 0 length read and check for bad file descriptor...
      ## but that segfaults on Win32
      my $test_fd = POSIX::dup( $fd );
      $in_use = defined $test_fd;
      POSIX::close $test_fd if $in_use;
      $map .= $in_use ? $digit : '-';
      $digit = 0 if ++$digit > 9;
   }
   warn "No fds open???" unless $map =~ /\d/;
   $map =~ s/(.{1,12})-*$/$1/;
   return $map;
}

use vars qw( $parent_pid );

$parent_pid = $$;

## TODO: move debugging to its own module and make it compile-time
## optimizable.

## Give kid process debugging nice names
my $debug_name;

sub _set_child_debug_name {
   $debug_name = shift;
}

## There's a bit of hackery going on here.
##
## We want to have any code anywhere be able to emit
## debugging statements without knowing what harness the code is
## being called in/from, since we'd need to pass a harness around to
## everything.
##
## Thus, $cur_self was born.
#
my %debug_levels = (
   none    => 0,
   basic   => 1,
   data    => 2,
   details => 3,
   gore           => 4,
   gory_details   => 4,
   "gory details" => 4,
   gory           => 4,
   gorydetails    => 4,
   all     => 10,
   notopt  => 0,
);

my $warned;

sub _debugging_level() {
   my $level = 0;

   $level = $IPC::Run::cur_self->{debug} || 0
      if $IPC::Run::cur_self
         && ( $IPC::Run::cur_self->{debug} || 0 ) >= $level;

   if ( defined $ENV{IPCRUNDEBUG} ) {
      my $v = $ENV{IPCRUNDEBUG};
      $v = $debug_levels{lc $v} if $v =~ /[a-zA-Z]/;
      unless ( defined $v ) {
	 $warned ||= warn "Unknown debug level $ENV{IPCRUNDEBUG}, assuming 'basic' (1)\n";
	 $v = 1;
      }
      $level = $v if $v > $level;
   }
   return $level;
}

sub _debugging_atleast($) {
   my $min_level = shift || 1;

   my $level = _debugging_level;
   
   return $level >= $min_level ? $level : 0;
}

sub _debugging()               { _debugging_atleast 1 }
sub _debugging_data()          { _debugging_atleast 2 }
sub _debugging_details()       { _debugging_atleast 3 }
sub _debugging_gory_details()  { _debugging_atleast 4 }
sub _debugging_not_optimized() { ( $ENV{IPCRUNDEBUG} || "" ) eq "notopt" }

sub _debug_init {
   ## This routine is called only in spawned children to fake out the
   ## debug routines so they'll emit debugging info.
   $IPC::Run::cur_self = {};
   (  $parent_pid,
      $^T, 
      $IPC::Run::cur_self->{debug}, 
      $IPC::Run::cur_self->{DEBUG_FD}, 
      $debug_name 
   ) = @_;
}


sub _debug {
#   return unless _debugging || _debugging_not_optimized;

   my $fd = defined &IPC::Run::_debug_fd
      ? IPC::Run::_debug_fd()
      : fileno STDERR;

   my $s;
   my $debug_id;
   $debug_id = join( 
      " ",
      join(
         "",
         defined $IPC::Run::cur_self ? "#$IPC::Run::cur_self->{ID}" : (),
         "($$)",
      ),
      defined $debug_name && length $debug_name ? $debug_name        : (),
   );
   my $prefix = join(
      "",
      "IPC::Run",
      sprintf( " %04d", time - $^T ),
      ( _debugging_details ? ( " ", _map_fds ) : () ),
      length $debug_id ? ( " [", $debug_id, "]" ) : (),
      ": ",
   );

   my $msg = join( '', map defined $_ ? $_ : "<undef>", @_ );
   chomp $msg;
   $msg =~ s{^}{$prefix}gm;
   $msg .= "\n";
   POSIX::write( $fd, $msg, length $msg );
}


my @fd_descs = ( 'stdin', 'stdout', 'stderr' );

sub _debug_desc_fd {
   return unless _debugging;
   my $text = shift;
   my $op = pop;
   my $kid = $_[0];

Carp::carp join " ", caller(0), $text, $op  if defined $op  && UNIVERSAL::isa( $op, "IO::Pty" );

   _debug(
      $text,
      ' ',
      ( defined $op->{FD}
         ? $op->{FD} < 3
            ? ( $fd_descs[$op->{FD}] )
            : ( 'fd ', $op->{FD} )
         : $op->{FD}
      ),
      ( defined $op->{KFD}
         ? (
            ' (kid',
            ( defined $kid ? ( ' ', $kid->{NUM}, ) : () ),
            "'s ",
            ( $op->{KFD} < 3
               ? $fd_descs[$op->{KFD}]
               : defined $kid
                  && defined $kid->{DEBUG_FD}
                  && $op->{KFD} == $kid->{DEBUG_FD}
                  ? ( 'debug (', $op->{KFD}, ')' )
                  : ( 'fd ', $op->{KFD} )
            ),
            ')',
         )
         : ()
      ),
   );
}

1;

SUBS

=pod

=head1 AUTHOR

Barrie Slaymaker <barries@slaysys.com>, with numerous suggestions by p5p.

=cut
