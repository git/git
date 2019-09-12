package IPC::Run::Timer;

=pod

=head1 NAME

IPC::Run::Timer -- Timer channels for IPC::Run.

=head1 SYNOPSIS

   use IPC::Run qw( run  timer timeout );
   ## or IPC::Run::Timer ( timer timeout );
   ## or IPC::Run::Timer ( :all );

   ## A non-fatal timer:
   $t = timer( 5 ); # or...
   $t = IO::Run::Timer->new( 5 );
   run $t, ...;

   ## A timeout (which is a timer that dies on expiry):
   $t = timeout( 5 ); # or...
   $t = IO::Run::Timer->new( 5, exception => "harness timed out" );

=head1 DESCRIPTION

This class and module allows timers and timeouts to be created for use
by IPC::Run.  A timer simply expires when it's time is up.  A timeout
is a timer that throws an exception when it expires.

Timeouts are usually a bit simpler to use  than timers: they throw an
exception on expiration so you don't need to check them:

   ## Give @cmd 10 seconds to get started, then 5 seconds to respond
   my $t = timeout( 10 );
   $h = start(
      \@cmd, \$in, \$out,
      $t,
   );
   pump $h until $out =~ /prompt/;

   $in = "some stimulus";
   $out = '';
   $t->time( 5 )
   pump $h until $out =~ /expected response/;

You do need to check timers:

   ## Give @cmd 10 seconds to get started, then 5 seconds to respond
   my $t = timer( 10 );
   $h = start(
      \@cmd, \$in, \$out,
      $t,
   );
   pump $h until $t->is_expired || $out =~ /prompt/;

   $in = "some stimulus";
   $out = '';
   $t->time( 5 )
   pump $h until $out =~ /expected response/ || $t->is_expired;

Timers and timeouts that are reset get started by start() and
pump().  Timers change state only in pump().  Since run() and
finish() both call pump(), they act like pump() with respect to
timers.

Timers and timeouts have three states: reset, running, and expired.
Setting the timeout value resets the timer, as does calling
the reset() method.  The start() method starts (or restarts) a
timer with the most recently set time value, no matter what state
it's in.

=head2 Time values

All time values are in seconds.  Times may be any kind of perl number,
e.g. as integer or floating point seconds, optionally preceded by
punctuation-separated days, hours, and minutes.

Examples:

   1           1 second
   1.1         1.1 seconds
   60          60 seconds
   1:0         1 minute
   1:1         1 minute, 1 second
   1:90        2 minutes, 30 seconds
   1:2:3:4.5   1 day, 2 hours, 3 minutes, 4.5 seconds
   'inf'       the infinity perl special number (the timer never finishes)

Absolute date/time strings are *not* accepted: year, month and
day-of-month parsing is not available (patches welcome :-).

=head2 Interval fudging

When calculating an end time from a start time and an interval, IPC::Run::Timer
instances add a little fudge factor.  This is to ensure that no time will
expire before the interval is up.

First a little background.  Time is sampled in discrete increments.  We'll
call the
exact moment that the reported time increments from one interval to the
next a tick, and the interval between ticks as the time period.  Here's
a diagram of three ticks and the periods between them:


    -0-0-0-0-0-0-0-0-0-0-1-1-1-1-1-1-1-1-1-1-2-...
    ^                   ^                   ^
    |<--- period 0 ---->|<--- period 1 ---->|
    |                   |                   |
  tick 0              tick 1              tick 2

To see why the fudge factor is necessary, consider what would happen
when a timer with an interval of 1 second is started right at the end of
period 0:


    -0-0-0-0-0-0-0-0-0-0-1-1-1-1-1-1-1-1-1-1-2-...
    ^                ^  ^                   ^
    |                |  |                   |
    |                |  |                   |
  tick 0             |tick 1              tick 2
                     |
                 start $t

Assuming that check() is called many times per period, then the timer
is likely to expire just after tick 1, since the time reported will have
lept from the value '0' to the value '1':

    -0-0-0-0-0-0-0-0-0-0-1-1-1-1-1-1-1-1-1-1-2-...
    ^                ^  ^   ^               ^
    |                |  |   |               |
    |                |  |   |               |
  tick 0             |tick 1|             tick 2
                     |      |
                 start $t   |
		            |
			check $t

Adding a fudge of '1' in this example means that the timer is guaranteed
not to expire before tick 2.

The fudge is not added to an interval of '0'.

This means that intervals guarantee a minimum interval.  Given that
the process running perl may be suspended for some period of time, or that
it gets busy doing something time-consuming, there are no other guarantees on
how long it will take a timer to expire.

=head1 SUBCLASSING

INCOMPATIBLE CHANGE: Due to the awkwardness introduced by ripping
pseudohashes out of Perl, this class I<no longer> uses the fields
pragma.

=head1 FUNCTIONS & METHODS

=over

=cut

use strict;
use Carp;
use Fcntl;
use Symbol;
use Exporter;
use Scalar::Util ();
use vars qw( $VERSION @ISA @EXPORT_OK %EXPORT_TAGS );

BEGIN {
    $VERSION   = '0.96';
    @ISA       = qw( Exporter );
    @EXPORT_OK = qw(
      check
      end_time
      exception
      expire
      interval
      is_expired
      is_reset
      is_running
      name
      reset
      start
      timeout
      timer
    );

    %EXPORT_TAGS = ( 'all' => \@EXPORT_OK );
}

require IPC::Run;
use IPC::Run::Debug;

##
## Some helpers
##
my $resolution = 1;

sub _parse_time {
    for ( $_[0] ) {
        my $val;
        if ( not defined $_ ) {
            $val = $_;
        }
        else {
            my @f = split( /:/, $_, -1 );
            if ( scalar @f > 4 ) {
                croak "IPC::Run: expected <= 4 elements in time string '$_'";
            }
            for (@f) {
                if ( not Scalar::Util::looks_like_number($_) ) {
                    croak "IPC::Run: non-numeric element '$_' in time string '$_'";
                }
            }
            my ( $s, $m, $h, $d ) = reverse @f;
            $val = ( ( ( $d || 0 ) * 24 + ( $h || 0 ) ) * 60 + ( $m || 0 ) ) * 60 + ( $s || 0 );
        }
        return $val;
    }
}

sub _calc_end_time {
    my IPC::Run::Timer $self = shift;
    my $interval = $self->interval;
    $interval += $resolution if $interval;
    $self->end_time( $self->start_time + $interval );
}

=item timer

A constructor function (not method) of IPC::Run::Timer instances:

   $t = timer( 5 );
   $t = timer( 5, name => 'stall timer', debug => 1 );

   $t = timer;
   $t->interval( 5 );

   run ..., $t;
   run ..., $t = timer( 5 );

This convenience function is a shortened spelling of

   IPC::Run::Timer->new( ... );
   
.  It returns a timer in the reset state with a given interval.

If an exception is provided, it will be thrown when the timer notices that
it has expired (in check()).  The name is for debugging usage, if you plan on
having multiple timers around.  If no name is provided, a name like "timer #1"
will be provided.

=cut

sub timer {
    return IPC::Run::Timer->new(@_);
}

=item timeout

A constructor function (not method) of IPC::Run::Timer instances:

   $t = timeout( 5 );
   $t = timeout( 5, exception => "kablooey" );
   $t = timeout( 5, name => "stall", exception => "kablooey" );

   $t = timeout;
   $t->interval( 5 );

   run ..., $t;
   run ..., $t = timeout( 5 );

A This convenience function is a shortened spelling of 

   IPC::Run::Timer->new( exception => "IPC::Run: timeout ...", ... );
   
.  It returns a timer in the reset state that will throw an
exception when it expires.

Takes the same parameters as L</timer>, any exception passed in overrides
the default exception.

=cut

sub timeout {
    my $t = IPC::Run::Timer->new(@_);
    $t->exception( "IPC::Run: timeout on " . $t->name )
      unless defined $t->exception;
    return $t;
}

=item new

   IPC::Run::Timer->new()  ;
   IPC::Run::Timer->new( 5 )  ;
   IPC::Run::Timer->new( 5, exception => 'kablooey' )  ;

Constructor.  See L</timer> for details.

=cut

my $timer_counter;

sub new {
    my $class = shift;
    $class = ref $class || $class;

    my IPC::Run::Timer $self = bless {}, $class;

    $self->{STATE} = 0;
    $self->{DEBUG} = 0;
    $self->{NAME}  = "timer #" . ++$timer_counter;

    while (@_) {
        my $arg = shift;
        if ( $arg eq 'exception' ) {
            $self->exception(shift);
        }
        elsif ( $arg eq 'name' ) {
            $self->name(shift);
        }
        elsif ( $arg eq 'debug' ) {
            $self->debug(shift);
        }
        else {
            $self->interval($arg);
        }
    }

    _debug $self->name . ' constructed'
      if $self->{DEBUG} || _debugging_details;

    return $self;
}

=item check

   check $t;
   check $t, $now;
   $t->check;

Checks to see if a timer has expired since the last check.  Has no effect
on non-running timers.  This will throw an exception if one is defined.

IPC::Run::pump() calls this routine for any timers in the harness.

You may pass in a version of now, which is useful in case you have
it lying around or you want to check several timers with a consistent
concept of the current time.

Returns the time left before end_time or 0 if end_time is no longer
in the future or the timer is not running
(unless, of course, check() expire()s the timer and this
results in an exception being thrown).

Returns undef if the timer is not running on entry, 0 if check() expires it,
and the time left if it's left running.

=cut

sub check {
    my IPC::Run::Timer $self = shift;
    return undef if !$self->is_running;
    return 0     if $self->is_expired;

    my ($now) = @_;
    $now = _parse_time($now);
    $now = time unless defined $now;

    _debug( "checking ", $self->name, " (end time ", $self->end_time, ") at ", $now ) if $self->{DEBUG} || _debugging_details;

    my $left = $self->end_time - $now;
    return $left if $left > 0;

    $self->expire;
    return 0;
}

=item debug

Sets/gets the current setting of the debugging flag for this timer.  This
has no effect if debugging is not enabled for the current harness.

=cut

sub debug {
    my IPC::Run::Timer $self = shift;
    $self->{DEBUG} = shift if @_;
    return $self->{DEBUG};
}

=item end_time

   $et = $t->end_time;
   $et = end_time $t;

   $t->end_time( time + 10 );

Returns the time when this timer will or did expire.  Even if this time is
in the past, the timer may not be expired, since check() may not have been
called yet.

Note that this end_time is not start_time($t) + interval($t), since some
small extra amount of time is added to make sure that the timer does not
expire before interval() elapses.  If this were not so, then 

Changing end_time() while a timer is running will set the expiration time.
Changing it while it is expired has no affect, since reset()ing a timer always
clears the end_time().

=cut

sub end_time {
    my IPC::Run::Timer $self = shift;
    if (@_) {
        $self->{END_TIME} = shift;
        _debug $self->name, ' end_time set to ', $self->{END_TIME}
          if $self->{DEBUG} > 2 || _debugging_details;
    }
    return $self->{END_TIME};
}

=item exception

   $x = $t->exception;
   $t->exception( $x );
   $t->exception( undef );

Sets/gets the exception to throw, if any.  'undef' means that no
exception will be thrown.  Exception does not need to be a scalar: you 
may ask that references be thrown.

=cut

sub exception {
    my IPC::Run::Timer $self = shift;
    if (@_) {
        $self->{EXCEPTION} = shift;
        _debug $self->name, ' exception set to ', $self->{EXCEPTION}
          if $self->{DEBUG} || _debugging_details;
    }
    return $self->{EXCEPTION};
}

=item interval

   $i = interval $t;
   $i = $t->interval;
   $t->interval( $i );

Sets the interval.  Sets the end time based on the start_time() and the
interval (and a little fudge) if the timer is running.

=cut

sub interval {
    my IPC::Run::Timer $self = shift;
    if (@_) {
        $self->{INTERVAL} = _parse_time(shift);
        _debug $self->name, ' interval set to ', $self->{INTERVAL}
          if $self->{DEBUG} > 2 || _debugging_details;

        $self->_calc_end_time if $self->state;
    }
    return $self->{INTERVAL};
}

=item expire

   expire $t;
   $t->expire;

Sets the state to expired (undef).
Will throw an exception if one
is defined and the timer was not already expired.  You can expire a
reset timer without starting it.

=cut

sub expire {
    my IPC::Run::Timer $self = shift;
    if ( defined $self->state ) {
        _debug $self->name . ' expired'
          if $self->{DEBUG} || _debugging;

        $self->state(undef);
        croak $self->exception if $self->exception;
    }
    return undef;
}

=item is_running

=cut

sub is_running {
    my IPC::Run::Timer $self = shift;
    return $self->state ? 1 : 0;
}

=item is_reset

=cut

sub is_reset {
    my IPC::Run::Timer $self = shift;
    return defined $self->state && $self->state == 0;
}

=item is_expired

=cut

sub is_expired {
    my IPC::Run::Timer $self = shift;
    return !defined $self->state;
}

=item name

Sets/gets this timer's name.  The name is only used for debugging
purposes so you can tell which freakin' timer is doing what.

=cut

sub name {
    my IPC::Run::Timer $self = shift;

    $self->{NAME} = shift if @_;
    return
        defined $self->{NAME}      ? $self->{NAME}
      : defined $self->{EXCEPTION} ? 'timeout'
      :                              'timer';
}

=item reset

   reset $t;
   $t->reset;

Resets the timer to the non-running, non-expired state and clears
the end_time().

=cut

sub reset {
    my IPC::Run::Timer $self = shift;
    $self->state(0);
    $self->end_time(undef);
    _debug $self->name . ' reset'
      if $self->{DEBUG} || _debugging;

    return undef;
}

=item start

   start $t;
   $t->start;
   start $t, $interval;
   start $t, $interval, $now;

Starts or restarts a timer.  This always sets the start_time.  It sets the
end_time based on the interval if the timer is running or if no end time
has been set.

You may pass an optional interval or current time value.

Not passing a defined interval causes the previous interval setting to be
re-used unless the timer is reset and an end_time has been set
(an exception is thrown if no interval has been set).  

Not passing a defined current time value causes the current time to be used.

Passing a current time value is useful if you happen to have a time value
lying around or if you want to make sure that several timers are started
with the same concept of start time.  You might even need to lie to an
IPC::Run::Timer, occasionally.

=cut

sub start {
    my IPC::Run::Timer $self = shift;

    my ( $interval, $now ) = map { _parse_time($_) } @_;
    $now = _parse_time($now);
    $now = time unless defined $now;

    $self->interval($interval) if defined $interval;

    ## start()ing a running or expired timer clears the end_time, so that the
    ## interval is used.  So does specifying an interval.
    $self->end_time(undef) if !$self->is_reset || $interval;

    croak "IPC::Run: no timer interval or end_time defined for " . $self->name
      unless defined $self->interval || defined $self->end_time;

    $self->state(1);
    $self->start_time($now);
    ## The "+ 1" is in case the START_TIME was sampled at the end of a
    ## tick (which are one second long in this module).
    $self->_calc_end_time
      unless defined $self->end_time;

    _debug(
        $self->name, " started at ", $self->start_time,
        ", with interval ", $self->interval, ", end_time ", $self->end_time
    ) if $self->{DEBUG} || _debugging;
    return undef;
}

=item start_time

Sets/gets the start time, in seconds since the epoch.  Setting this manually
is a bad idea, it's better to call L</start>() at the correct time.

=cut

sub start_time {
    my IPC::Run::Timer $self = shift;
    if (@_) {
        $self->{START_TIME} = _parse_time(shift);
        _debug $self->name, ' start_time set to ', $self->{START_TIME}
          if $self->{DEBUG} > 2 || _debugging;
    }

    return $self->{START_TIME};
}

=item state

   $s = state $t;
   $t->state( $s );

Get/Set the current state.  Only use this if you really need to transfer the
state to/from some variable.
Use L</expire>, L</start>, L</reset>, L</is_expired>, L</is_running>,
L</is_reset>.

Note:  Setting the state to 'undef' to expire a timer will not throw an
exception.

=back

=cut

sub state {
    my IPC::Run::Timer $self = shift;
    if (@_) {
        $self->{STATE} = shift;
        _debug $self->name, ' state set to ', $self->{STATE}
          if $self->{DEBUG} > 2 || _debugging;
    }
    return $self->{STATE};
}

1;

=pod

=head1 TODO

use Time::HiRes; if it's present.

Add detection and parsing of [[[HH:]MM:]SS formatted times and intervals.

=head1 AUTHOR

Barrie Slaymaker <barries@slaysys.com>

=cut
