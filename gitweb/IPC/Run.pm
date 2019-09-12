package IPC::Run;
use bytes;

=pod

=head1 NAME

IPC::Run - system() and background procs w/ piping, redirs, ptys (Unix, Win32)

=head1 SYNOPSIS

   ## First,a command to run:
      my @cat = qw( cat );

   ## Using run() instead of system():
      use IPC::Run qw( run timeout );

      run \@cmd, \$in, \$out, \$err, timeout( 10 ) or die "cat: $?"

      # Can do I/O to sub refs and filenames, too:
      run \@cmd, '<', "in.txt", \&out, \&err or die "cat: $?"
      run \@cat, '<', "in.txt", '>>', "out.txt", '2>>', "err.txt";


      # Redirecting using pseudo-terminals instead of pipes.
      run \@cat, '<pty<', \$in,  '>pty>', \$out_and_err;

   ## Scripting subprocesses (like Expect):

      use IPC::Run qw( start pump finish timeout );

      # Incrementally read from / write to scalars. 
      # $in is drained as it is fed to cat's stdin,
      # $out accumulates cat's stdout
      # $err accumulates cat's stderr
      # $h is for "harness".
      my $h = start \@cat, \$in, \$out, \$err, timeout( 10 );

      $in .= "some input\n";
      pump $h until $out =~ /input\n/g;

      $in .= "some more input\n";
      pump $h until $out =~ /\G.*more input\n/;

      $in .= "some final input\n";
      finish $h or die "cat returned $?";

      warn $err if $err; 
      print $out;         ## All of cat's output

   # Piping between children
      run \@cat, '|', \@gzip;

   # Multiple children simultaneously (run() blocks until all
   # children exit, use start() for background execution):
      run \@foo1, '&', \@foo2;

   # Calling \&set_up_child in the child before it executes the
   # command (only works on systems with true fork() & exec())
   # exceptions thrown in set_up_child() will be propagated back
   # to the parent and thrown from run().
      run \@cat, \$in, \$out,
         init => \&set_up_child;

   # Read from / write to file handles you open and close
      open IN,  '<in.txt'  or die $!;
      open OUT, '>out.txt' or die $!;
      print OUT "preamble\n";
      run \@cat, \*IN, \*OUT or die "cat returned $?";
      print OUT "postamble\n";
      close IN;
      close OUT;

   # Create pipes for you to read / write (like IPC::Open2 & 3).
      $h = start
         \@cat,
            '<pipe', \*IN,
            '>pipe', \*OUT,
            '2>pipe', \*ERR 
         or die "cat returned $?";
      print IN "some input\n";
      close IN;
      print <OUT>, <ERR>;
      finish $h;

   # Mixing input and output modes
      run \@cat, 'in.txt', \&catch_some_out, \*ERR_LOG );

   # Other redirection constructs
      run \@cat, '>&', \$out_and_err;
      run \@cat, '2>&1';
      run \@cat, '0<&3';
      run \@cat, '<&-';
      run \@cat, '3<', \$in3;
      run \@cat, '4>', \$out4;
      # etc.

   # Passing options:
      run \@cat, 'in.txt', debug => 1;

   # Call this system's shell, returns TRUE on 0 exit code
   # THIS IS THE OPPOSITE SENSE OF system()'s RETURN VALUE
      run "cat a b c" or die "cat returned $?";

   # Launch a sub process directly, no shell.  Can't do redirection
   # with this form, it's here to behave like system() with an
   # inverted result.
      $r = run "cat a b c";

   # Read from a file in to a scalar
      run io( "filename", 'r', \$recv );
      run io( \*HANDLE,   'r', \$recv );

=head1 DESCRIPTION

IPC::Run allows you to run and interact with child processes using files, pipes,
and pseudo-ttys.  Both system()-style and scripted usages are supported and
may be mixed.  Likewise, functional and OO API styles are both supported and
may be mixed.

Various redirection operators reminiscent of those seen on common Unix and DOS
command lines are provided.

Before digging in to the details a few LIMITATIONS are important enough
to be mentioned right up front:

=over

=item Win32 Support

Win32 support is working but B<EXPERIMENTAL>, but does pass all relevant tests
on NT 4.0.  See L</Win32 LIMITATIONS>.

=item pty Support

If you need pty support, IPC::Run should work well enough most of the
time, but IO::Pty is being improved, and IPC::Run will be improved to
use IO::Pty's new features when it is release.

The basic problem is that the pty needs to initialize itself before the
parent writes to the master pty, or the data written gets lost.  So
IPC::Run does a sleep(1) in the parent after forking to (hopefully) give
the child a chance to run.  This is a kludge that works well on non
heavily loaded systems :(.

ptys are not supported yet under Win32, but will be emulated...

=item Debugging Tip

You may use the environment variable C<IPCRUNDEBUG> to see what's going on
under the hood:

   $ IPCRUNDEBUG=basic   myscript     # prints minimal debugging
   $ IPCRUNDEBUG=data    myscript     # prints all data reads/writes
   $ IPCRUNDEBUG=details myscript     # prints lots of low-level details
   $ IPCRUNDEBUG=gory    myscript     # (Win32 only) prints data moving through
                                      # the helper processes.

=back

We now return you to your regularly scheduled documentation.

=head2 Harnesses

Child processes and I/O handles are gathered in to a harness, then
started and run until the processing is finished or aborted.

=head2 run() vs. start(); pump(); finish();

There are two modes you can run harnesses in: run() functions as an
enhanced system(), and start()/pump()/finish() allow for background
processes and scripted interactions with them.

When using run(), all data to be sent to the harness is set up in
advance (though one can feed subprocesses input from subroutine refs to
get around this limitation). The harness is run and all output is
collected from it, then any child processes are waited for:

   run \@cmd, \<<IN, \$out;
   blah
   IN

   ## To precompile harnesses and run them later:
   my $h = harness \@cmd, \<<IN, \$out;
   blah
   IN

   run $h;

The background and scripting API is provided by start(), pump(), and
finish(): start() creates a harness if need be (by calling harness())
and launches any subprocesses, pump() allows you to poll them for
activity, and finish() then monitors the harnessed activities until they
complete.

   ## Build the harness, open all pipes, and launch the subprocesses
   my $h = start \@cat, \$in, \$out;
   $in = "first input\n";

   ## Now do I/O.  start() does no I/O.
   pump $h while length $in;  ## Wait for all input to go

   ## Now do some more I/O.
   $in = "second input\n";
   pump $h until $out =~ /second input/;

   ## Clean up
   finish $h or die "cat returned $?";

You can optionally compile the harness with harness() prior to
start()ing or run()ing, and you may omit start() between harness() and
pump().  You might want to do these things if you compile your harnesses
ahead of time.

=head2 Using regexps to match output

As shown in most of the scripting examples, the read-to-scalar facility
for gathering subcommand's output is often used with regular expressions
to detect stopping points.  This is because subcommand output often
arrives in dribbles and drabs, often only a character or line at a time.
This output is input for the main program and piles up in variables like
the C<$out> and C<$err> in our examples.

Regular expressions can be used to wait for appropriate output in
several ways.  The C<cat> example in the previous section demonstrates
how to pump() until some string appears in the output.  Here's an
example that uses C<smb> to fetch files from a remote server:

   $h = harness \@smbclient, \$in, \$out;

   $in = "cd /src\n";
   $h->pump until $out =~ /^smb.*> \Z/m;
   die "error cding to /src:\n$out" if $out =~ "ERR";
   $out = '';

   $in = "mget *\n";
   $h->pump until $out =~ /^smb.*> \Z/m;
   die "error retrieving files:\n$out" if $out =~ "ERR";

   $in = "quit\n";
   $h->finish;

Notice that we carefully clear $out after the first command/response
cycle? That's because IPC::Run does not delete $out when we continue,
and we don't want to trip over the old output in the second
command/response cycle.

Say you want to accumulate all the output in $out and analyze it
afterwards.  Perl offers incremental regular expression matching using
the C<m//gc> and pattern matching idiom and the C<\G> assertion.
IPC::Run is careful not to disturb the current C<pos()> value for
scalars it appends data to, so we could modify the above so as not to
destroy $out by adding a couple of C</gc> modifiers.  The C</g> keeps us
from tripping over the previous prompt and the C</c> keeps us from
resetting the prior match position if the expected prompt doesn't
materialize immediately:

   $h = harness \@smbclient, \$in, \$out;

   $in = "cd /src\n";
   $h->pump until $out =~ /^smb.*> \Z/mgc;
   die "error cding to /src:\n$out" if $out =~ "ERR";

   $in = "mget *\n";
   $h->pump until $out =~ /^smb.*> \Z/mgc;
   die "error retrieving files:\n$out" if $out =~ "ERR";

   $in = "quit\n";
   $h->finish;

   analyze( $out );

When using this technique, you may want to preallocate $out to have
plenty of memory or you may find that the act of growing $out each time
new input arrives causes an C<O(length($out)^2)> slowdown as $out grows.
Say we expect no more than 10,000 characters of input at the most.  To
preallocate memory to $out, do something like:

   my $out = "x" x 10_000;
   $out = "";

C<perl> will allocate at least 10,000 characters' worth of space, then
mark the $out as having 0 length without freeing all that yummy RAM.

=head2 Timeouts and Timers

More than likely, you don't want your subprocesses to run forever, and
sometimes it's nice to know that they're going a little slowly.
Timeouts throw exceptions after a some time has elapsed, timers merely
cause pump() to return after some time has elapsed.  Neither is
reset/restarted automatically.

Timeout objects are created by calling timeout( $interval ) and passing
the result to run(), start() or harness().  The timeout period starts
ticking just after all the child processes have been fork()ed or
spawn()ed, and are polled for expiration in run(), pump() and finish().
If/when they expire, an exception is thrown.  This is typically useful
to keep a subprocess from taking too long.

If a timeout occurs in run(), all child processes will be terminated and
all file/pipe/ptty descriptors opened by run() will be closed.  File
descriptors opened by the parent process and passed in to run() are not
closed in this event.

If a timeout occurs in pump(), pump_nb(), or finish(), it's up to you to
decide whether to kill_kill() all the children or to implement some more
graceful fallback.  No I/O will be closed in pump(), pump_nb() or
finish() by such an exception (though I/O is often closed down in those
routines during the natural course of events).

Often an exception is too harsh.  timer( $interval ) creates timer
objects that merely prevent pump() from blocking forever.  This can be
useful for detecting stalled I/O or printing a soothing message or "."
to pacify an anxious user.

Timeouts and timers can both be restarted at any time using the timer's
start() method (this is not the start() that launches subprocesses).  To
restart a timer, you need to keep a reference to the timer:

   ## Start with a nice long timeout to let smbclient connect.  If
   ## pump or finish take too long, an exception will be thrown.

 my $h;
 eval {
   $h = harness \@smbclient, \$in, \$out, \$err, ( my $t = timeout 30 );
   sleep 11;  # No effect: timer not running yet

   start $h;
   $in = "cd /src\n";
   pump $h until ! length $in;

   $in = "ls\n";
   ## Now use a short timeout, since this should be faster
   $t->start( 5 );
   pump $h until ! length $in;

   $t->start( 10 );  ## Give smbclient a little while to shut down.
   $h->finish;
 };
 if ( $@ ) {
   my $x = $@;    ## Preserve $@ in case another exception occurs
   $h->kill_kill; ## kill it gently, then brutally if need be, or just
                   ## brutally on Win32.
   die $x;
 }

Timeouts and timers are I<not> checked once the subprocesses are shut
down; they will not expire in the interval between the last valid
process and when IPC::Run scoops up the processes' result codes, for
instance.

=head2 Spawning synchronization, child exception propagation

start() pauses the parent until the child executes the command or CODE
reference and propagates any exceptions thrown (including exec()
failure) back to the parent.  This has several pleasant effects: any
exceptions thrown in the child, including exec() failure, come flying
out of start() or run() as though they had occurred in the parent.

This includes exceptions your code thrown from init subs.  In this
example:

   eval {
      run \@cmd, init => sub { die "blast it! foiled again!" };
   };
   print $@;

the exception "blast it! foiled again" will be thrown from the child
process (preventing the exec()) and printed by the parent.

In situations like

   run \@cmd1, "|", \@cmd2, "|", \@cmd3;

@cmd1 will be initted and exec()ed before @cmd2, and @cmd2 before @cmd3.
This can save time and prevent oddball errors emitted by later commands
when earlier commands fail to execute.  Note that IPC::Run doesn't start
any commands unless it can find the executables referenced by all
commands.  These executables must pass both the C<-f> and C<-x> tests
described in L<perlfunc>.

Another nice effect is that init() subs can take their time doing things
and there will be no problems caused by a parent continuing to execute
before a child's init() routine is complete.  Say the init() routine
needs to open a socket or a temp file that the parent wants to connect
to; without this synchronization, the parent will need to implement a
retry loop to wait for the child to run, since often, the parent gets a
lot of things done before the child's first timeslice is allocated.

This is also quite necessary for pseudo-tty initialization, which needs
to take place before the parent writes to the child via pty.  Writes
that occur before the pty is set up can get lost.

A final, minor, nicety is that debugging output from the child will be
emitted before the parent continues on, making for much clearer debugging
output in complex situations.

The only drawback I can conceive of is that the parent can't continue to
operate while the child is being initted.  If this ever becomes a
problem in the field, we can implement an option to avoid this behavior,
but I don't expect it to.

B<Win32>: executing CODE references isn't supported on Win32, see
L</Win32 LIMITATIONS> for details.

=head2 Syntax

run(), start(), and harness() can all take a harness specification
as input.  A harness specification is either a single string to be passed
to the systems' shell:

   run "echo 'hi there'";

or a list of commands, io operations, and/or timers/timeouts to execute.
Consecutive commands must be separated by a pipe operator '|' or an '&'.
External commands are passed in as array references, and, on systems
supporting fork(), Perl code may be passed in as subs:

   run \@cmd;
   run \@cmd1, '|', \@cmd2;
   run \@cmd1, '&', \@cmd2;
   run \&sub1;
   run \&sub1, '|', \&sub2;
   run \&sub1, '&', \&sub2;

'|' pipes the stdout of \@cmd1 the stdin of \@cmd2, just like a
shell pipe.  '&' does not.  Child processes to the right of a '&'
will have their stdin closed unless it's redirected-to.

L<IPC::Run::IO> objects may be passed in as well, whether or not
child processes are also specified:

   run io( "infile", ">", \$in ), io( "outfile", "<", \$in );
      
as can L<IPC::Run::Timer> objects:

   run \@cmd, io( "outfile", "<", \$in ), timeout( 10 );

Commands may be followed by scalar, sub, or i/o handle references for
redirecting
child process input & output:

   run \@cmd,  \undef,            \$out;
   run \@cmd,  \$in,              \$out;
   run \@cmd1, \&in, '|', \@cmd2, \*OUT;
   run \@cmd1, \*IN, '|', \@cmd2, \&out;

This is known as succinct redirection syntax, since run(), start()
and harness(), figure out which file descriptor to redirect and how.
File descriptor 0 is presumed to be an input for
the child process, all others are outputs.  The assumed file
descriptor always starts at 0, unless the command is being piped to,
in which case it starts at 1.

To be explicit about your redirects, or if you need to do more complex
things, there's also a redirection operator syntax:

   run \@cmd, '<', \undef, '>',  \$out;
   run \@cmd, '<', \undef, '>&', \$out_and_err;
   run(
      \@cmd1,
         '<', \$in,
      '|', \@cmd2,
         \$out
   );

Operator syntax is required if you need to do something other than simple
redirection to/from scalars or subs, like duping or closing file descriptors
or redirecting to/from a named file.  The operators are covered in detail
below.

After each \@cmd (or \&foo), parsing begins in succinct mode and toggles to
operator syntax mode when an operator (ie plain scalar, not a ref) is seen.
Once in
operator syntax mode, parsing only reverts to succinct mode when a '|' or
'&' is seen.

In succinct mode, each parameter after the \@cmd specifies what to
do with the next highest file descriptor. These File descriptor start
with 0 (stdin) unless stdin is being piped to (C<'|', \@cmd>), in which
case they start with 1 (stdout).  Currently, being on the left of
a pipe (C<\@cmd, \$out, \$err, '|'>) does I<not> cause stdout to be
skipped, though this may change since it's not as DWIMerly as it
could be.  Only stdin is assumed to be an
input in succinct mode, all others are assumed to be outputs.

If no piping or redirection is specified for a child, it will inherit
the parent's open file handles as dictated by your system's
close-on-exec behavior and the $^F flag, except that processes after a
'&' will not inherit the parent's stdin. Also note that $^F does not
affect file descriptors obtained via POSIX, since it only applies to
full-fledged Perl file handles.  Such processes will have their stdin
closed unless it has been redirected-to.

If you want to close a child processes stdin, you may do any of:

   run \@cmd, \undef;
   run \@cmd, \"";
   run \@cmd, '<&-';
   run \@cmd, '0<&-';

Redirection is done by placing redirection specifications immediately 
after a command or child subroutine:

   run \@cmd1,      \$in, '|', \@cmd2,      \$out;
   run \@cmd1, '<', \$in, '|', \@cmd2, '>', \$out;

If you omit the redirection operators, descriptors are counted
starting at 0.  Descriptor 0 is assumed to be input, all others
are outputs.  A leading '|' consumes descriptor 0, so this
works as expected.

   run \@cmd1, \$in, '|', \@cmd2, \$out;
   
The parameter following a redirection operator can be a scalar ref,
a subroutine ref, a file name, an open filehandle, or a closed
filehandle.

If it's a scalar ref, the child reads input from or sends output to
that variable:

   $in = "Hello World.\n";
   run \@cat, \$in, \$out;
   print $out;

Scalars used in incremental (start()/pump()/finish()) applications are treated
as queues: input is removed from input scalers, resulting in them dwindling
to '', and output is appended to output scalars.  This is not true of 
harnesses run() in batch mode.

It's usually wise to append new input to be sent to the child to the input
queue, and you'll often want to zap output queues to '' before pumping.

   $h = start \@cat, \$in;
   $in = "line 1\n";
   pump $h;
   $in .= "line 2\n";
   pump $h;
   $in .= "line 3\n";
   finish $h;

The final call to finish() must be there: it allows the child process(es)
to run to completion and waits for their exit values.

=head1 OBSTINATE CHILDREN

Interactive applications are usually optimized for human use.  This
can help or hinder trying to interact with them through modules like
IPC::Run.  Frequently, programs alter their behavior when they detect
that stdin, stdout, or stderr are not connected to a tty, assuming that
they are being run in batch mode.  Whether this helps or hurts depends
on which optimizations change.  And there's often no way of telling
what a program does in these areas other than trial and error and
occasionally, reading the source.  This includes different versions
and implementations of the same program.

All hope is not lost, however.  Most programs behave in reasonably
tractable manners, once you figure out what it's trying to do.

Here are some of the issues you might need to be aware of.

=over

=item *

fflush()ing stdout and stderr

This lets the user see stdout and stderr immediately.  Many programs
undo this optimization if stdout is not a tty, making them harder to
manage by things like IPC::Run.

Many programs decline to fflush stdout or stderr if they do not
detect a tty there.  Some ftp commands do this, for instance.

If this happens to you, look for a way to force interactive behavior,
like a command line switch or command.  If you can't, you will
need to use a pseudo terminal ('<pty<' and '>pty>').

=item *

false prompts

Interactive programs generally do not guarantee that output from user
commands won't contain a prompt string.  For example, your shell prompt
might be a '$', and a file named '$' might be the only file in a directory
listing.

This can make it hard to guarantee that your output parser won't be fooled
into early termination of results.

To help work around this, you can see if the program can alter it's 
prompt, and use something you feel is never going to occur in actual
practice.

You should also look for your prompt to be the only thing on a line:

   pump $h until $out =~ /^<SILLYPROMPT>\s?\z/m;

(use C<(?!\n)\Z> in place of C<\z> on older perls).

You can also take the approach that IPC::ChildSafe takes and emit a
command with known output after each 'real' command you issue, then
look for this known output.  See new_appender() and new_chunker() for
filters that can help with this task.

If it's not convenient or possibly to alter a prompt or use a known
command/response pair, you might need to autodetect the prompt in case
the local version of the child program is different then the one
you tested with, or if the user has control over the look & feel of
the prompt.

=item *

Refusing to accept input unless stdin is a tty.

Some programs, for security reasons, will only accept certain types
of input from a tty.  su, notable, will not prompt for a password unless
it's connected to a tty.

If this is your situation, use a pseudo terminal ('<pty<' and '>pty>').

=item *

Not prompting unless connected to a tty.

Some programs don't prompt unless stdin or stdout is a tty.  See if you can
turn prompting back on.  If not, see if you can come up with a command that
you can issue after every real command and look for it's output, as
IPC::ChildSafe does.   There are two filters included with IPC::Run that
can help with doing this: appender and chunker (see new_appender() and
new_chunker()).

=item *

Different output format when not connected to a tty.

Some commands alter their formats to ease machine parsability when they
aren't connected to a pipe.  This is actually good, but can be surprising.

=back

=head1 PSEUDO TERMINALS

On systems providing pseudo terminals under /dev, IPC::Run can use IO::Pty
(available on CPAN) to provide a terminal environment to subprocesses.
This is necessary when the subprocess really wants to think it's connected
to a real terminal.

=head2 CAVEATS

Pseudo-terminals are not pipes, though they are similar.  Here are some
differences to watch out for.

=over

=item Echoing

Sending to stdin will cause an echo on stdout, which occurs before each
line is passed to the child program.  There is currently no way to
disable this, although the child process can and should disable it for
things like passwords.

=item Shutdown

IPC::Run cannot close a pty until all output has been collected.  This
means that it is not possible to send an EOF to stdin by half-closing
the pty, as we can when using a pipe to stdin.

This means that you need to send the child process an exit command or
signal, or run() / finish() will time out.  Be careful not to expect a
prompt after sending the exit command.

=item Command line editing

Some subprocesses, notable shells that depend on the user's prompt
settings, will reissue the prompt plus the command line input so far
once for each character.

=item '>pty>' means '&>pty>', not '1>pty>'

The pseudo terminal redirects both stdout and stderr unless you specify
a file descriptor.  If you want to grab stderr separately, do this:

   start \@cmd, '<pty<', \$in, '>pty>', \$out, '2>', \$err;

=item stdin, stdout, and stderr not inherited

Child processes harnessed to a pseudo terminal have their stdin, stdout,
and stderr completely closed before any redirection operators take
effect.  This casts of the bonds of the controlling terminal.  This is
not done when using pipes.

Right now, this affects all children in a harness that has a pty in use,
even if that pty would not affect a particular child.  That's a bug and
will be fixed.  Until it is, it's best not to mix-and-match children.

=back

=head2 Redirection Operators

   Operator       SHNP   Description
   ========       ====   ===========
   <, N<          SHN    Redirects input to a child's fd N (0 assumed)

   >, N>          SHN    Redirects output from a child's fd N (1 assumed)
   >>, N>>        SHN    Like '>', but appends to scalars or named files
   >&, &>         SHN    Redirects stdout & stderr from a child process

   <pty, N<pty    S      Like '<', but uses a pseudo-tty instead of a pipe
   >pty, N>pty    S      Like '>', but uses a pseudo-tty instead of a pipe

   N<&M                  Dups input fd N to input fd M
   M>&N                  Dups output fd N to input fd M
   N<&-                  Closes fd N

   <pipe, N<pipe     P   Pipe opens H for caller to read, write, close.
   >pipe, N>pipe     P   Pipe opens H for caller to read, write, close.
                      
'N' and 'M' are placeholders for integer file descriptor numbers.  The
terms 'input' and 'output' are from the child process's perspective.

The SHNP field indicates what parameters an operator can take:

   S: \$scalar or \&function references.  Filters may be used with
      these operators (and only these).
   H: \*HANDLE or IO::Handle for caller to open, and close
   N: "file name".
   P: \*HANDLE opened by IPC::Run as the parent end of a pipe, but read
      and written to and closed by the caller (like IPC::Open3).

=over

=item Redirecting input: [n]<, [n]<pipe

You can input the child reads on file descriptor number n to come from a
scalar variable, subroutine, file handle, or a named file.  If stdin
is not redirected, the parent's stdin is inherited.

   run \@cat, \undef          ## Closes child's stdin immediately
      or die "cat returned $?"; 

   run \@cat, \$in;

   run \@cat, \<<TOHERE;
   blah
   TOHERE

   run \@cat, \&input;       ## Calls &input, feeding data returned
                              ## to child's.  Closes child's stdin
                              ## when undef is returned.

Redirecting from named files requires you to use the input
redirection operator:

   run \@cat, '<.profile';
   run \@cat, '<', '.profile';

   open IN, "<foo";
   run \@cat, \*IN;
   run \@cat, *IN{IO};

The form used second example here is the safest,
since filenames like "0" and "&more\n" won't confuse &run:

You can't do either of

   run \@a, *IN;      ## INVALID
   run \@a, '<', *IN; ## BUGGY: Reads file named like "*main::A"
   
because perl passes a scalar containing a string that
looks like "*main::A" to &run, and &run can't tell the difference
between that and a redirection operator or a file name.  &run guarantees
that any scalar you pass after a redirection operator is a file name.

If your child process will take input from file descriptors other
than 0 (stdin), you can use a redirection operator with any of the
valid input forms (scalar ref, sub ref, etc.):

   run \@cat, '3<', \$in3;

When redirecting input from a scalar ref, the scalar ref is
used as a queue.  This allows you to use &harness and pump() to
feed incremental bits of input to a coprocess.  See L</Coprocesses>
below for more information.

The <pipe operator opens the write half of a pipe on the filehandle
glob reference it takes as an argument:

   $h = start \@cat, '<pipe', \*IN;
   print IN "hello world\n";
   pump $h;
   close IN;
   finish $h;

Unlike the other '<' operators, IPC::Run does nothing further with
it: you are responsible for it.  The previous example is functionally
equivalent to:

   pipe( \*R, \*IN ) or die $!;
   $h = start \@cat, '<', \*IN;
   print IN "hello world\n";
   pump $h;
   close IN;
   finish $h;

This is like the behavior of IPC::Open2 and IPC::Open3.

B<Win32>: The handle returned is actually a socket handle, so you can
use select() on it.

=item Redirecting output: [n]>, [n]>>, [n]>&[m], [n]>pipe

You can redirect any output the child emits
to a scalar variable, subroutine, file handle, or file name.  You
can have &run truncate or append to named files or scalars.  If
you are redirecting stdin as well, or if the command is on the
receiving end of a pipeline ('|'), you can omit the redirection
operator:

   @ls = ( 'ls' );
   run \@ls, \undef, \$out
      or die "ls returned $?"; 

   run \@ls, \undef, \&out;  ## Calls &out each time some output
                              ## is received from the child's 
                              ## when undef is returned.

   run \@ls, \undef, '2>ls.err';
   run \@ls, '2>', 'ls.err';

The two parameter form guarantees that the filename
will not be interpreted as a redirection operator:

   run \@ls, '>', "&more";
   run \@ls, '2>', ">foo\n";

You can pass file handles you've opened for writing:

   open( *OUT, ">out.txt" );
   open( *ERR, ">err.txt" );
   run \@cat, \*OUT, \*ERR;

Passing a scalar reference and a code reference requires a little
more work, but allows you to capture all of the output in a scalar
or each piece of output by a callback:

These two do the same things:

   run( [ 'ls' ], '2>', sub { $err_out .= $_[0] } );

does the same basic thing as:

   run( [ 'ls' ], '2>', \$err_out );

The subroutine will be called each time some data is read from the child.

The >pipe operator is different in concept than the other '>' operators,
although it's syntax is similar:

   $h = start \@cat, $in, '>pipe', \*OUT, '2>pipe', \*ERR;
   $in = "hello world\n";
   finish $h;
   print <OUT>;
   print <ERR>;
   close OUT;
   close ERR;

causes two pipe to be created, with one end attached to cat's stdout
and stderr, respectively, and the other left open on OUT and ERR, so
that the script can manually
read(), select(), etc. on them.  This is like
the behavior of IPC::Open2 and IPC::Open3.

B<Win32>: The handle returned is actually a socket handle, so you can
use select() on it.

=item Duplicating output descriptors: >&m, n>&m

This duplicates output descriptor number n (default is 1 if n is omitted)
from descriptor number m.

=item Duplicating input descriptors: <&m, n<&m

This duplicates input descriptor number n (default is 0 if n is omitted)
from descriptor number m

=item Closing descriptors: <&-, 3<&-

This closes descriptor number n (default is 0 if n is omitted).  The
following commands are equivalent:

   run \@cmd, \undef;
   run \@cmd, '<&-';
   run \@cmd, '<in.txt', '<&-';

Doing

   run \@cmd, \$in, '<&-';    ## SIGPIPE recipe.

is dangerous: the parent will get a SIGPIPE if $in is not empty.

=item Redirecting both stdout and stderr: &>, >&, &>pipe, >pipe&

The following pairs of commands are equivalent:

   run \@cmd, '>&', \$out;       run \@cmd, '>', \$out,     '2>&1';
   run \@cmd, '>&', 'out.txt';   run \@cmd, '>', 'out.txt', '2>&1';

etc.

File descriptor numbers are not permitted to the left or the right of
these operators, and the '&' may occur on either end of the operator.

The '&>pipe' and '>pipe&' variants behave like the '>pipe' operator, except
that both stdout and stderr write to the created pipe.

=item Redirection Filters

Both input redirections and output redirections that use scalars or
subs as endpoints may have an arbitrary number of filter subs placed
between them and the child process.  This is useful if you want to
receive output in chunks, or if you want to massage each chunk of
data sent to the child.  To use this feature, you must use operator
syntax:

   run(
      \@cmd
         '<', \&in_filter_2, \&in_filter_1, $in,
         '>', \&out_filter_1, \&in_filter_2, $out,
   );

This capability is not provided for IO handles or named files.

Two filters are provided by IPC::Run: appender and chunker.  Because
these may take an argument, you need to use the constructor functions
new_appender() and new_chunker() rather than using \& syntax:

   run(
      \@cmd
         '<', new_appender( "\n" ), $in,
         '>', new_chunker, $out,
   );

=back

=head2 Just doing I/O

If you just want to do I/O to a handle or file you open yourself, you
may specify a filehandle or filename instead of a command in the harness
specification:

   run io( "filename", '>', \$recv );

   $h = start io( $io, '>', \$recv );

   $h = harness \@cmd, '&', io( "file", '<', \$send );

=head2 Options

Options are passed in as name/value pairs:

   run \@cat, \$in, debug => 1;

If you pass the debug option, you may want to pass it in first, so you
can see what parsing is going on:

   run debug => 1, \@cat, \$in;

=over

=item debug

Enables debugging output in parent and child.  Debugging info is emitted
to the STDERR that was present when IPC::Run was first C<use()>ed (it's
C<dup()>ed out of the way so that it can be redirected in children without
having debugging output emitted on it).

=back

=head1 RETURN VALUES

harness() and start() return a reference to an IPC::Run harness.  This is
blessed in to the IPC::Run package, so you may make later calls to
functions as members if you like:

   $h = harness( ... );
   $h->start;
   $h->pump;
   $h->finish;

   $h = start( .... );
   $h->pump;
   ...

Of course, using method call syntax lets you deal with any IPC::Run
subclasses that might crop up, but don't hold your breath waiting for
any.

run() and finish() return TRUE when all subcommands exit with a 0 result
code.  B<This is the opposite of perl's system() command>.

All routines raise exceptions (via die()) when error conditions are
recognized.  A non-zero command result is not treated as an error
condition, since some commands are tests whose results are reported 
in their exit codes.

=head1 ROUTINES

=over

=cut

use strict;
use Exporter ();
use vars qw{$VERSION @ISA @FILTER_IMP @FILTERS @API @EXPORT_OK %EXPORT_TAGS};

BEGIN {
    $VERSION = '0.96';
    @ISA     = qw{ Exporter };

    ## We use @EXPORT for the end user's convenience: there's only one function
    ## exported, it's homonymous with the module, it's an unusual name, and
    ## it can be suppressed by "use IPC::Run ();".
    @FILTER_IMP = qw( input_avail get_more_input );
    @FILTERS    = qw(
      new_appender
      new_chunker
      new_string_source
      new_string_sink
    );
    @API = qw(
      run
      harness start pump pumpable finish
      signal kill_kill reap_nb
      io timer timeout
      close_terminal
      binary
    );
    @EXPORT_OK = ( @API, @FILTER_IMP, @FILTERS, qw( Win32_MODE ) );
    %EXPORT_TAGS = (
        'filter_imp' => \@FILTER_IMP,
        'all'        => \@EXPORT_OK,
        'filters'    => \@FILTERS,
        'api'        => \@API,
    );

}

use strict;
use IPC::Run::Debug;
use Exporter;
use Fcntl;
use POSIX ();

BEGIN {
    if ( $] < 5.008 ) { require Symbol; }
}
use Carp;
use File::Spec ();
use IO::Handle;
require IPC::Run::IO;
require IPC::Run::Timer;

use constant Win32_MODE => $^O =~ /os2|Win32/i;

BEGIN {
    if (Win32_MODE) {
        eval "use IPC::Run::Win32Helper; 1;"
          or ( $@ && die )
          or die "$!";
    }
    else {
        eval "use File::Basename; 1;" or die $!;
    }
}

sub input_avail();
sub get_more_input();

###############################################################################

##
## Error constants, not too locale-dependent
use vars qw( $_EIO $_EAGAIN );
use Errno qw(   EIO   EAGAIN );

BEGIN {
    local $!;
    $!       = EIO;
    $_EIO    = qr/^$!/;
    $!       = EAGAIN;
    $_EAGAIN = qr/^$!/;
}

##
## State machine states, set in $self->{STATE}
##
## These must be in ascending order numerically
##
sub _newed()     { 0 }
sub _harnessed() { 1 }
sub _finished()  { 2 }    ## _finished behave almost exactly like _harnessed
sub _started()   { 3 }

##
## Which fds have been opened in the parent.  This may have extra fds, since
## we aren't all that rigorous about closing these off, but that's ok.  This
## is used on Unixish OSs to close all fds in the child that aren't needed
## by that particular child.
my %fds;

## There's a bit of hackery going on here.
##
## We want to have any code anywhere be able to emit
## debugging statements without knowing what harness the code is
## being called in/from, since we'd need to pass a harness around to
## everything.
##
## Thus, $cur_self was born.

use vars qw( $cur_self );

sub _debug_fd {
    return fileno STDERR unless defined $cur_self;

    if ( _debugging && !defined $cur_self->{DEBUG_FD} ) {
        my $fd = select STDERR;
        $| = 1;
        select $fd;
        $cur_self->{DEBUG_FD} = POSIX::dup fileno STDERR;
        _debug("debugging fd is $cur_self->{DEBUG_FD}\n")
          if _debugging_details;
    }

    return fileno STDERR unless defined $cur_self->{DEBUG_FD};

    return $cur_self->{DEBUG_FD};
}

sub DESTROY {
    ## We absolutely do not want to do anything else here.  We are likely
    ## to be in a child process and we don't want to do things like kill_kill
    ## ourself or cause other destruction.
    my IPC::Run $self = shift;
    POSIX::close $self->{DEBUG_FD} if defined $self->{DEBUG_FD};
    $self->{DEBUG_FD} = undef;
}

##
## Support routines (NOT METHODS)
##
my %cmd_cache;

sub _search_path {
    my ($cmd_name) = @_;
    if ( File::Spec->file_name_is_absolute($cmd_name) && -x $cmd_name ) {
        _debug "'", $cmd_name, "' is absolute"
          if _debugging_details;
        return $cmd_name;
    }

    my $dirsep = (
          Win32_MODE     ? '[/\\\\]'
        : $^O =~ /MacOS/ ? ':'
        : $^O =~ /VMS/   ? '[\[\]]'
        :                  '/'
    );

    if (
        Win32_MODE
        && ( $cmd_name =~ /$dirsep/ )

        #      && ( $cmd_name !~ /\..+$/ )  ## Only run if cmd_name has no extension?
        && ( $cmd_name !~ m!\.[^\\/\.]+$! )
      ) {

        _debug "no extension(.exe), checking ENV{PATHEXT}" if _debugging;
        for ( split /;/, $ENV{PATHEXT} || ".COM;.BAT;.EXE" ) {
            my $name = "$cmd_name$_";
            $cmd_name = $name, last if -f $name && -x _;
        }
        _debug "cmd_name is now '$cmd_name'" if _debugging;
    }

    if ( $cmd_name =~ /($dirsep)/ ) {
        _debug "'$cmd_name' contains '$1'" if _debugging;
        croak "file not found: $cmd_name"    unless -e $cmd_name;
        croak "not a file: $cmd_name"        unless -f $cmd_name;
        croak "permission denied: $cmd_name" unless -x $cmd_name;
        return $cmd_name;
    }

    if ( exists $cmd_cache{$cmd_name} ) {
        _debug "'$cmd_name' found in cache: '$cmd_cache{$cmd_name}'"
          if _debugging;
        return $cmd_cache{$cmd_name} if -x $cmd_cache{$cmd_name};
        _debug "'$cmd_cache{$cmd_name}' no longer executable, searching..."
          if _debugging;
        delete $cmd_cache{$cmd_name};
    }

    my @searched_in;

    ## This next bit is Unix/Win32 specific, unfortunately.
    ## There's been some conversation about extending File::Spec to provide
    ## a universal interface to PATH, but I haven't seen it yet.
    my $re = Win32_MODE ? qr/;/ : qr/:/;

  LOOP:
    for ( split( $re, $ENV{PATH} || '', -1 ) ) {
        $_ = "." unless length $_;
        push @searched_in, $_;

        my $prospect = File::Spec->catfile( $_, $cmd_name );
        my @prospects;

        @prospects =
          ( Win32_MODE && !( -f $prospect && -x _ ) )
          ? map "$prospect$_", split /;/, $ENV{PATHEXT} || ".COM;.BAT;.EXE"
          : ($prospect);

        for my $found (@prospects) {
            if ( -f $found && -x _ ) {
                $cmd_cache{$cmd_name} = $found;
                last LOOP;
            }
        }
    }

    if ( exists $cmd_cache{$cmd_name} ) {
        _debug "'", $cmd_name, "' added to cache: '", $cmd_cache{$cmd_name}, "'"
          if _debugging_details;
        return $cmd_cache{$cmd_name};
    }

    croak "Command '$cmd_name' not found in " . join( ", ", @searched_in );
}

sub _empty($) { !( defined $_[0] && length $_[0] ) }

## 'safe' versions of otherwise fun things to do. See also IPC::Run::Win32Helper.
sub _close {
    confess 'undef' unless defined $_[0];
    my $fd = $_[0] =~ /^\d+$/ ? $_[0] : fileno $_[0];
    my $r = POSIX::close $fd;
    $r = $r ? '' : " ERROR $!";
    delete $fds{$fd};
    _debug "close( $fd ) = " . ( $r || 0 ) if _debugging_details;
}

sub _dup {
    confess 'undef' unless defined $_[0];
    my $r = POSIX::dup( $_[0] );
    croak "$!: dup( $_[0] )" unless defined $r;
    $r = 0 if $r eq '0 but true';
    _debug "dup( $_[0] ) = $r" if _debugging_details;
    $fds{$r} = 1;
    return $r;
}

sub _dup2_rudely {
    confess 'undef' unless defined $_[0] && defined $_[1];
    my $r = POSIX::dup2( $_[0], $_[1] );
    croak "$!: dup2( $_[0], $_[1] )" unless defined $r;
    $r = 0 if $r eq '0 but true';
    _debug "dup2( $_[0], $_[1] ) = $r" if _debugging_details;
    $fds{$r} = 1;
    return $r;
}

sub _exec {
    confess 'undef passed' if grep !defined, @_;

    #   exec @_ or croak "$!: exec( " . join( ', ', @_ ) . " )";
    _debug 'exec()ing ', join " ", map "'$_'", @_ if _debugging_details;

    #   {
## Commented out since we don't call this on Win32.
    #      # This works around the bug where 5.6.1 complains
    #      # "Can't exec ...: No error" after an exec on NT, where
    #      # exec() is simulated and actually returns in Perl's C
    #      # code, though Perl's &exec does not...
    #      no warnings "exec";
    #
    #      # Just in case the no warnings workaround
    #      # stops being a workaround, we don't want
    #      # old values of $! causing spurious strerr()
    #      # messages to appear in the "Can't exec" message
    #      undef $!;
    exec { $_[0] } @_;

    #   }
    #   croak "$!: exec( " . join( ', ', map "'$_'", @_ ) . " )";
    ## Fall through so $! can be reported to parent.
}

sub _sysopen {
    confess 'undef' unless defined $_[0] && defined $_[1];
    _debug sprintf( "O_RDONLY=0x%02x ", O_RDONLY ),
      sprintf( "O_WRONLY=0x%02x ", O_WRONLY ),
      sprintf( "O_RDWR=0x%02x ",   O_RDWR ),
      sprintf( "O_TRUNC=0x%02x ",  O_TRUNC ),
      sprintf( "O_CREAT=0x%02x ",  O_CREAT ),
      sprintf( "O_APPEND=0x%02x ", O_APPEND ),
      if _debugging_details;
    my $r = POSIX::open( $_[0], $_[1], 0644 );
    croak "$!: open( $_[0], ", sprintf( "0x%03x", $_[1] ), " )" unless defined $r;
    _debug "open( $_[0], ", sprintf( "0x%03x", $_[1] ), " ) = $r"
      if _debugging_data;
    $fds{$r} = 1;
    return $r;
}

sub _pipe {
    ## Normal, blocking write for pipes that we read and the child writes,
    ## since most children expect writes to stdout to block rather than
    ## do a partial write.
    my ( $r, $w ) = POSIX::pipe;
    croak "$!: pipe()" unless defined $r;
    _debug "pipe() = ( $r, $w ) " if _debugging_details;
    $fds{$r} = $fds{$w} = 1;
    return ( $r, $w );
}

sub _pipe_nb {
    ## For pipes that we write, unblock the write side, so we can fill a buffer
    ## and continue to select().
    ## Contributed by Borislav Deianov <borislav@ensim.com>, with minor
    ## bugfix on fcntl result by me.
    local ( *R, *W );
    my $f = pipe( R, W );
    croak "$!: pipe()" unless defined $f;
    my ( $r, $w ) = ( fileno R, fileno W );
    _debug "pipe_nb pipe() = ( $r, $w )" if _debugging_details;
    unless (Win32_MODE) {
        ## POSIX::fcntl doesn't take fd numbers, so gotta use Perl's and
        ## then _dup the originals (which get closed on leaving this block)
        my $fres = fcntl( W, &F_SETFL, O_WRONLY | O_NONBLOCK );
        croak "$!: fcntl( $w, F_SETFL, O_NONBLOCK )" unless $fres;
        _debug "fcntl( $w, F_SETFL, O_NONBLOCK )" if _debugging_details;
    }
    ( $r, $w ) = ( _dup($r), _dup($w) );
    _debug "pipe_nb() = ( $r, $w )" if _debugging_details;
    return ( $r, $w );
}

sub _pty {
    require IO::Pty;
    my $pty = IO::Pty->new();
    croak "$!: pty ()" unless $pty;
    $pty->autoflush();
    $pty->blocking(0) or croak "$!: pty->blocking ( 0 )";
    _debug "pty() = ( ", $pty->fileno, ", ", $pty->slave->fileno, " )"
      if _debugging_details;
    $fds{ $pty->fileno } = $fds{ $pty->slave->fileno } = 1;
    return $pty;
}

sub _read {
    confess 'undef' unless defined $_[0];
    my $s = '';
    my $r = POSIX::read( $_[0], $s, 10_000 );
    croak "$!: read( $_[0] )" if not($r) and $! != POSIX::EINTR();
    $r ||= 0;
    _debug "read( $_[0] ) = $r chars '$s'" if _debugging_data;
    return $s;
}

## A METHOD, not a function.
sub _spawn {
    my IPC::Run $self = shift;
    my ($kid) = @_;

    _debug "opening sync pipe ", $kid->{PID} if _debugging_details;
    my $sync_reader_fd;
    ( $sync_reader_fd, $self->{SYNC_WRITER_FD} ) = _pipe;
    $kid->{PID} = fork();
    croak "$! during fork" unless defined $kid->{PID};

    unless ( $kid->{PID} ) {
        ## _do_kid_and_exit closes sync_reader_fd since it closes all unwanted and
        ## unloved fds.
        $self->_do_kid_and_exit($kid);
    }
    _debug "fork() = ", $kid->{PID} if _debugging_details;

    ## Wait for kid to get to it's exec() and see if it fails.
    _close $self->{SYNC_WRITER_FD};
    my $sync_pulse = _read $sync_reader_fd;
    _close $sync_reader_fd;

    if ( !defined $sync_pulse || length $sync_pulse ) {
        if ( waitpid( $kid->{PID}, 0 ) >= 0 ) {
            $kid->{RESULT} = $?;
        }
        else {
            $kid->{RESULT} = -1;
        }
        $sync_pulse = "error reading synchronization pipe for $kid->{NUM}, pid $kid->{PID}"
          unless length $sync_pulse;
        croak $sync_pulse;
    }
    return $kid->{PID};

## Wait for pty to get set up.  This is a hack until we get synchronous
## selects.
    if ( keys %{ $self->{PTYS} } && $IO::Pty::VERSION < 0.9 ) {
        _debug "sleeping to give pty a chance to init, will fix when newer IO::Pty arrives.";
        sleep 1;
    }
}

sub _write {
    confess 'undef' unless defined $_[0] && defined $_[1];
    my $r = POSIX::write( $_[0], $_[1], length $_[1] );
    croak "$!: write( $_[0], '$_[1]' )" unless $r;
    _debug "write( $_[0], '$_[1]' ) = $r" if _debugging_data;
    return $r;
}

=pod

=over

=item run

Run takes a harness or harness specification and runs it, pumping
all input to the child(ren), closing the input pipes when no more
input is available, collecting all output that arrives, until the
pipes delivering output are closed, then waiting for the children to
exit and reaping their result codes.

You may think of C<run( ... )> as being like 

   start( ... )->finish();

, though there is one subtle difference: run() does not
set \$input_scalars to '' like finish() does.  If an exception is thrown
from run(), all children will be killed off "gently", and then "annihilated"
if they do not go gently (in to that dark night. sorry).

If any exceptions are thrown, this does a L</kill_kill> before propagating
them.

=cut

use vars qw( $in_run );    ## No, not Enron;)

sub run {
    local $in_run = 1;     ## Allow run()-only optimizations.
    my IPC::Run $self = start(@_);
    my $r = eval {
        $self->{clear_ins} = 0;
        $self->finish;
    };
    if ($@) {
        my $x = $@;
        $self->kill_kill;
        die $x;
    }
    return $r;
}

=pod

=item signal

   ## To send it a specific signal by name ("USR1"):
   signal $h, "USR1";
   $h->signal ( "USR1" );

If $signal is provided and defined, sends a signal to all child processes.  Try
not to send numeric signals, use C<"KILL"> instead of C<9>, for instance.
Numeric signals aren't portable.

Throws an exception if $signal is undef.

This will I<not> clean up the harness, C<finish> it if you kill it.

Normally TERM kills a process gracefully (this is what the command line utility
C<kill> does by default), INT is sent by one of the keys C<^C>, C<Backspace> or
C<E<lt>DelE<gt>>, and C<QUIT> is used to kill a process and make it coredump.

The C<HUP> signal is often used to get a process to "restart", rereading 
config files, and C<USR1> and C<USR2> for really application-specific things.

Often, running C<kill -l> (that's a lower case "L") on the command line will
list the signals present on your operating system.

B<WARNING>: The signal subsystem is not at all portable.  We *may* offer
to simulate C<TERM> and C<KILL> on some operating systems, submit code
to me if you want this.

B<WARNING 2>: Up to and including perl v5.6.1, doing almost anything in a
signal handler could be dangerous.  The most safe code avoids all
mallocs and system calls, usually by preallocating a flag before
entering the signal handler, altering the flag's value in the
handler, and responding to the changed value in the main system:

   my $got_usr1 = 0;
   sub usr1_handler { ++$got_signal }

   $SIG{USR1} = \&usr1_handler;
   while () { sleep 1; print "GOT IT" while $got_usr1--; }

Even this approach is perilous if ++ and -- aren't atomic on your system
(I've never heard of this on any modern CPU large enough to run perl).

=cut

sub signal {
    my IPC::Run $self = shift;

    local $cur_self = $self;

    $self->_kill_kill_kill_pussycat_kill unless @_;

    Carp::cluck "Ignoring extra parameters passed to kill()" if @_ > 1;

    my ($signal) = @_;
    croak "Undefined signal passed to signal" unless defined $signal;
    for ( grep $_->{PID} && !defined $_->{RESULT}, @{ $self->{KIDS} } ) {
        _debug "sending $signal to $_->{PID}"
          if _debugging;
        kill $signal, $_->{PID}
          or _debugging && _debug "$! sending $signal to $_->{PID}";
    }

    return;
}

=pod

=item kill_kill

   ## To kill off a process:
   $h->kill_kill;
   kill_kill $h;

   ## To specify the grace period other than 30 seconds:
   kill_kill $h, grace => 5;

   ## To send QUIT instead of KILL if a process refuses to die:
   kill_kill $h, coup_d_grace => "QUIT";

Sends a C<TERM>, waits for all children to exit for up to 30 seconds, then
sends a C<KILL> to any that survived the C<TERM>.

Will wait for up to 30 more seconds for the OS to successfully C<KILL> the
processes.

The 30 seconds may be overridden by setting the C<grace> option, this
overrides both timers.

The harness is then cleaned up.

The doubled name indicates that this function may kill again and avoids
colliding with the core Perl C<kill> function.

Returns a 1 if the C<TERM> was sufficient, or a 0 if C<KILL> was 
required.  Throws an exception if C<KILL> did not permit the children
to be reaped.

B<NOTE>: The grace period is actually up to 1 second longer than that
given.  This is because the granularity of C<time> is 1 second.  Let me
know if you need finer granularity, we can leverage Time::HiRes here.

B<Win32>: Win32 does not know how to send real signals, so C<TERM> is
a full-force kill on Win32.  Thus all talk of grace periods, etc. do
not apply to Win32.

=cut

sub kill_kill {
    my IPC::Run $self = shift;

    my %options = @_;
    my $grace   = $options{grace};
    $grace = 30 unless defined $grace;
    ++$grace;    ## Make grace time a _minimum_

    my $coup_d_grace = $options{coup_d_grace};
    $coup_d_grace = "KILL" unless defined $coup_d_grace;

    delete $options{$_} for qw( grace coup_d_grace );
    Carp::cluck "Ignoring unknown options for kill_kill: ",
      join " ", keys %options
      if keys %options;

    $self->signal("TERM");

    my $quitting_time = time + $grace;
    my $delay         = 0.01;
    my $accum_delay;

    my $have_killed_before;

    while () {
        ## delay first to yield to other processes
        select undef, undef, undef, $delay;
        $accum_delay += $delay;

        $self->reap_nb;
        last unless $self->_running_kids;

        if ( $accum_delay >= $grace * 0.8 ) {
            ## No point in checking until delay has grown some.
            if ( time >= $quitting_time ) {
                if ( !$have_killed_before ) {
                    $self->signal($coup_d_grace);
                    $have_killed_before = 1;
                    $quitting_time += $grace;
                    $delay       = 0.01;
                    $accum_delay = 0;
                    next;
                }
                croak "Unable to reap all children, even after KILLing them";
            }
        }

        $delay *= 2;
        $delay = 0.5 if $delay >= 0.5;
    }

    $self->_cleanup;
    return $have_killed_before;
}

=pod

=item harness

Takes a harness specification and returns a harness.  This harness is
blessed in to IPC::Run, allowing you to use method call syntax for
run(), start(), et al if you like.

harness() is provided so that you can pre-build harnesses if you
would like to, but it's not required..

You may proceed to run(), start() or pump() after calling harness() (pump()
calls start() if need be).  Alternatively, you may pass your
harness specification to run() or start() and let them harness() for
you.  You can't pass harness specifications to pump(), though.

=cut

##
## Notes: I've avoided handling a scalar that doesn't look like an
## opcode as a here document or as a filename, though I could DWIM
## those.  I'm not sure that the advantages outweigh the danger when
## the DWIMer guesses wrong.
##
## TODO: allow user to spec default shell. Hmm, globally, in the
## lexical scope hash, or per instance?  'Course they can do that
## now by using a [...] to hold the command.
##
my $harness_id = 0;

sub harness {
    my $options;
    if ( @_ && ref $_[-1] eq 'HASH' ) {
        $options = pop;
        require Data::Dumper;
        carp "Passing in options as a hash is deprecated:\n", Data::Dumper::Dumper($options);
    }

    #   local $IPC::Run::debug = $options->{debug}
    #      if $options && defined $options->{debug};

    my @args;
    if ( @_ == 1 && !ref $_[0] ) {
        if (Win32_MODE) {
            my $command = $ENV{ComSpec} || 'cmd';
            @args = ( [ $command, '/c', win32_parse_cmd_line $_[0] ] );
        }
        else {
            @args = ( [ qw( sh -c ), @_ ] );
        }
    }
    elsif ( @_ > 1 && !grep ref $_, @_ ) {
        @args = ( [@_] );
    }
    else {
        @args = @_;
    }

    my @errs;    # Accum errors, emit them when done.

    my $succinct;    # set if no redir ops are required yet.  Cleared
                     # if an op is seen.

    my $cur_kid;     # references kid or handle being parsed

    my $assumed_fd = 0;    # fd to assume in succinct mode (no redir ops)
    my $handle_num = 0;    # 1... is which handle we're parsing

    my IPC::Run $self = bless {}, __PACKAGE__;

    local $cur_self = $self;

    $self->{ID}    = ++$harness_id;
    $self->{IOS}   = [];
    $self->{KIDS}  = [];
    $self->{PIPES} = [];
    $self->{PTYS}  = {};
    $self->{STATE} = _newed;

    if ($options) {
        $self->{$_} = $options->{$_} for keys %$options;
    }

    _debug "****** harnessing *****" if _debugging;

    my $first_parse;
    local $_;
    my $arg_count = @args;
    while (@args) {
        for ( shift @args ) {
            eval {
                $first_parse = 1;
                _debug(
                    "parsing ",
                    defined $_
                    ? ref $_ eq 'ARRAY'
                          ? ( '[ ', join( ', ', map "'$_'", @$_ ), ' ]' )
                          : (
                              ref $_
                                || (
                                  length $_ < 50
                                  ? "'$_'"
                                  : join( '', "'", substr( $_, 0, 10 ), "...'" )
                                )
                          )
                    : '<undef>'
                ) if _debugging;

              REPARSE:
                if ( ref eq 'ARRAY' || ( !$cur_kid && ref eq 'CODE' ) ) {
                    croak "Process control symbol ('|', '&') missing" if $cur_kid;
                    croak "Can't spawn a subroutine on Win32"
                      if Win32_MODE && ref eq "CODE";
                    $cur_kid = {
                        TYPE   => 'cmd',
                        VAL    => $_,
                        NUM    => @{ $self->{KIDS} } + 1,
                        OPS    => [],
                        PID    => '',
                        RESULT => undef,
                    };
                    push @{ $self->{KIDS} }, $cur_kid;
                    $succinct = 1;
                }

                elsif ( UNIVERSAL::isa( $_, 'IPC::Run::IO' ) ) {
                    push @{ $self->{IOS} }, $_;
                    $cur_kid  = undef;
                    $succinct = 1;
                }

                elsif ( UNIVERSAL::isa( $_, 'IPC::Run::Timer' ) ) {
                    push @{ $self->{TIMERS} }, $_;
                    $cur_kid  = undef;
                    $succinct = 1;
                }

                elsif (/^(\d*)>&(\d+)$/) {
                    croak "No command before '$_'" unless $cur_kid;
                    push @{ $cur_kid->{OPS} }, {
                        TYPE => 'dup',
                        KFD1 => $2,
                        KFD2 => length $1 ? $1 : 1,
                    };
                    _debug "redirect operators now required" if _debugging_details;
                    $succinct = !$first_parse;
                }

                elsif (/^(\d*)<&(\d+)$/) {
                    croak "No command before '$_'" unless $cur_kid;
                    push @{ $cur_kid->{OPS} }, {
                        TYPE => 'dup',
                        KFD1 => $2,
                        KFD2 => length $1 ? $1 : 0,
                    };
                    $succinct = !$first_parse;
                }

                elsif (/^(\d*)<&-$/) {
                    croak "No command before '$_'" unless $cur_kid;
                    push @{ $cur_kid->{OPS} }, {
                        TYPE => 'close',
                        KFD => length $1 ? $1 : 0,
                    };
                    $succinct = !$first_parse;
                }

                elsif (/^(\d*) (<pipe)()            ()  ()  $/x
                    || /^(\d*) (<pty) ((?:\s+\S+)?) (<) ()  $/x
                    || /^(\d*) (<)    ()            ()  (.*)$/x ) {
                    croak "No command before '$_'" unless $cur_kid;

                    $succinct = !$first_parse;

                    my $type = $2 . $4;

                    my $kfd = length $1 ? $1 : 0;

                    my $pty_id;
                    if ( $type eq '<pty<' ) {
                        $pty_id = length $3 ? $3 : '0';
                        ## do the require here to cause early error reporting
                        require IO::Pty;
                        ## Just flag the pyt's existence for now.  It'll be
                        ## converted to a real IO::Pty by _open_pipes.
                        $self->{PTYS}->{$pty_id} = undef;
                    }

                    my $source = $5;

                    my @filters;
                    my $binmode;

                    unless ( length $source ) {
                        if ( !$succinct ) {
                            while ( @args > 1
                                && ( ( ref $args[1] && !UNIVERSAL::isa $args[1], "IPC::Run::Timer" ) || UNIVERSAL::isa $args[0], "IPC::Run::binmode_pseudo_filter" ) ) {
                                if ( UNIVERSAL::isa $args[0], "IPC::Run::binmode_pseudo_filter" ) {
                                    $binmode = shift(@args)->();
                                }
                                else {
                                    push @filters, shift @args;
                                }
                            }
                        }
                        $source = shift @args;
                        croak "'$_' missing a source" if _empty $source;

                        _debug(
                            'Kid ',  $cur_kid->{NUM},  "'s input fd ", $kfd,
                            ' has ', scalar(@filters), ' filters.'
                        ) if _debugging_details && @filters;
                    }

                    my IPC::Run::IO $pipe = IPC::Run::IO->_new_internal( $type, $kfd, $pty_id, $source, $binmode, @filters );

                    if ( ( ref $source eq 'GLOB' || UNIVERSAL::isa $source, 'IO::Handle' )
                        && $type !~ /^<p(ty<|ipe)$/ ) {
                        _debug "setting DONT_CLOSE" if _debugging_details;
                        $pipe->{DONT_CLOSE} = 1;    ## this FD is not closed by us.
                        _dont_inherit($source) if Win32_MODE;
                    }

                    push @{ $cur_kid->{OPS} }, $pipe;
                }

                elsif (
                       /^()   (>>?)  (&)     ()      (.*)$/x
                    || /^()   (&)    (>pipe) ()      ()  $/x
                    || /^()   (>pipe)(&)     ()      ()  $/x
                    || /^(\d*)()     (>pipe) ()      ()  $/x
                    || /^()   (&)    (>pty)  ( \w*)> ()  $/x
## TODO:    ||   /^()   (>pty) (\d*)> (&) ()  $/x
                    || /^(\d*)()     (>pty)  ( \w*)> ()  $/x
                    || /^()   (&)    (>>?)   ()      (.*)$/x || /^(\d*)()     (>>?)   ()      (.*)$/x
                  ) {
                    croak "No command before '$_'" unless $cur_kid;

                    $succinct = !$first_parse;

                    my $type = (
                          $2 eq '>pipe' || $3 eq '>pipe' ? '>pipe'
                        : $2 eq '>pty'  || $3 eq '>pty'  ? '>pty>'
                        :                                  '>'
                    );
                    my $kfd = length $1 ? $1 : 1;
                    my $trunc = !( $2 eq '>>' || $3 eq '>>' );
                    my $pty_id = (
                          $2 eq '>pty' || $3 eq '>pty'
                        ? length $4
                              ? $4
                              : 0
                        : undef
                    );

                    my $stderr_too =
                         $2 eq '&'
                      || $3 eq '&'
                      || ( !length $1 && substr( $type, 0, 4 ) eq '>pty' );

                    my $dest = $5;
                    my @filters;
                    my $binmode = 0;
                    unless ( length $dest ) {
                        if ( !$succinct ) {
                            ## unshift...shift: '>' filters source...sink left...right
                            while ( @args > 1
                                && ( ( ref $args[1] && !UNIVERSAL::isa $args[1], "IPC::Run::Timer" ) || UNIVERSAL::isa $args[0], "IPC::Run::binmode_pseudo_filter" ) ) {
                                if ( UNIVERSAL::isa $args[0], "IPC::Run::binmode_pseudo_filter" ) {
                                    $binmode = shift(@args)->();
                                }
                                else {
                                    unshift @filters, shift @args;
                                }
                            }
                        }

                        $dest = shift @args;

                        _debug(
                            'Kid ',  $cur_kid->{NUM},  "'s output fd ", $kfd,
                            ' has ', scalar(@filters), ' filters.'
                        ) if _debugging_details && @filters;

                        if ( $type eq '>pty>' ) {
                            ## do the require here to cause early error reporting
                            require IO::Pty;
                            ## Just flag the pyt's existence for now.  _open_pipes()
                            ## will new an IO::Pty for each key.
                            $self->{PTYS}->{$pty_id} = undef;
                        }
                    }

                    croak "'$_' missing a destination" if _empty $dest;
                    my $pipe = IPC::Run::IO->_new_internal( $type, $kfd, $pty_id, $dest, $binmode, @filters );
                    $pipe->{TRUNC} = $trunc;

                    if ( ( UNIVERSAL::isa( $dest, 'GLOB' ) || UNIVERSAL::isa( $dest, 'IO::Handle' ) )
                        && $type !~ /^>(pty>|pipe)$/ ) {
                        _debug "setting DONT_CLOSE" if _debugging_details;
                        $pipe->{DONT_CLOSE} = 1;    ## this FD is not closed by us.
                    }
                    push @{ $cur_kid->{OPS} }, $pipe;
                    push @{ $cur_kid->{OPS} }, {
                        TYPE => 'dup',
                        KFD1 => 1,
                        KFD2 => 2,
                    } if $stderr_too;
                }

                elsif ( $_ eq "|" ) {
                    croak "No command before '$_'" unless $cur_kid;
                    unshift @{ $cur_kid->{OPS} }, {
                        TYPE => '|',
                        KFD  => 1,
                    };
                    $succinct   = 1;
                    $assumed_fd = 1;
                    $cur_kid    = undef;
                }

                elsif ( $_ eq "&" ) {
                    croak "No command before '$_'" unless $cur_kid;
                    unshift @{ $cur_kid->{OPS} }, {
                        TYPE => 'close',
                        KFD  => 0,
                    };
                    $succinct   = 1;
                    $assumed_fd = 0;
                    $cur_kid    = undef;
                }

                elsif ( $_ eq 'init' ) {
                    croak "No command before '$_'" unless $cur_kid;
                    push @{ $cur_kid->{OPS} }, {
                        TYPE => 'init',
                        SUB  => shift @args,
                    };
                }

                elsif ( !ref $_ ) {
                    $self->{$_} = shift @args;
                }

                elsif ( $_ eq 'init' ) {
                    croak "No command before '$_'" unless $cur_kid;
                    push @{ $cur_kid->{OPS} }, {
                        TYPE => 'init',
                        SUB  => shift @args,
                    };
                }

                elsif ( $succinct && $first_parse ) {
                    ## It's not an opcode, and no explicit opcodes have been
                    ## seen yet, so assume it's a file name.
                    unshift @args, $_;
                    if ( !$assumed_fd ) {
                        $_ = "$assumed_fd<",
                    }
                    else {
                        $_ = "$assumed_fd>",
                    }
                    _debug "assuming '", $_, "'" if _debugging_details;
                    ++$assumed_fd;
                    $first_parse = 0;
                    goto REPARSE;
                }

                else {
                    croak join(
                        '',
                        'Unexpected ',
                        ( ref() ? $_ : 'scalar' ),
                        ' in harness() parameter ',
                        $arg_count - @args
                    );
                }
            };
            if ($@) {
                push @errs, $@;
                _debug 'caught ', $@ if _debugging;
            }
        }
    }

    die join( '', @errs ) if @errs;

    $self->{STATE} = _harnessed;

    #   $self->timeout( $options->{timeout} ) if exists $options->{timeout};
    return $self;
}

sub _open_pipes {
    my IPC::Run $self = shift;

    my @errs;

    my @close_on_fail;

    ## When a pipe character is seen, a pipe is created.  $pipe_read_fd holds
    ## the dangling read end of the pipe until we get to the next process.
    my $pipe_read_fd;

    ## Output descriptors for the last command are shared by all children.
    ## @output_fds_accum accumulates the current set of output fds.
    my @output_fds_accum;

    for ( sort keys %{ $self->{PTYS} } ) {
        _debug "opening pty '", $_, "'" if _debugging_details;
        my $pty = _pty;
        $self->{PTYS}->{$_} = $pty;
    }

    for ( @{ $self->{IOS} } ) {
        eval { $_->init; };
        if ($@) {
            push @errs, $@;
            _debug 'caught ', $@ if _debugging;
        }
        else {
            push @close_on_fail, $_;
        }
    }

    ## Loop through the kids and their OPS, interpreting any that require
    ## parent-side actions.
    for my $kid ( @{ $self->{KIDS} } ) {
        unless ( ref $kid->{VAL} eq 'CODE' ) {
            $kid->{PATH} = _search_path $kid->{VAL}->[0];
        }
        if ( defined $pipe_read_fd ) {
            _debug "placing write end of pipe on kid $kid->{NUM}'s stdin"
              if _debugging_details;
            unshift @{ $kid->{OPS} }, {
                TYPE => 'PIPE',          ## Prevent next loop from triggering on this
                KFD  => 0,
                TFD  => $pipe_read_fd,
            };
            $pipe_read_fd = undef;
        }
        @output_fds_accum = ();
        for my $op ( @{ $kid->{OPS} } ) {

            #         next if $op->{IS_DEBUG};
            my $ok = eval {
                if ( $op->{TYPE} eq '<' ) {
                    my $source = $op->{SOURCE};
                    if ( !ref $source ) {
                        _debug(
                            "kid ",              $kid->{NUM}, " to read ", $op->{KFD},
                            " from '" . $source, "' (read only)"
                        ) if _debugging_details;
                        croak "simulated open failure"
                          if $self->{_simulate_open_failure};
                        $op->{TFD} = _sysopen( $source, O_RDONLY );
                        push @close_on_fail, $op->{TFD};
                    }
                    elsif (UNIVERSAL::isa( $source, 'GLOB' )
                        || UNIVERSAL::isa( $source, 'IO::Handle' ) ) {
                        croak "Unopened filehandle in input redirect for $op->{KFD}"
                          unless defined fileno $source;
                        $op->{TFD} = fileno $source;
                        _debug(
                            "kid ",      $kid->{NUM}, " to read ", $op->{KFD},
                            " from fd ", $op->{TFD}
                        ) if _debugging_details;
                    }
                    elsif ( UNIVERSAL::isa( $source, 'SCALAR' ) ) {
                        _debug(
                            "kid ", $kid->{NUM}, " to read ", $op->{KFD},
                            " from SCALAR"
                        ) if _debugging_details;

                        $op->open_pipe( $self->_debug_fd );
                        push @close_on_fail, $op->{KFD}, $op->{FD};

                        my $s = '';
                        $op->{KIN_REF} = \$s;
                    }
                    elsif ( UNIVERSAL::isa( $source, 'CODE' ) ) {
                        _debug( 'kid ', $kid->{NUM}, ' to read ', $op->{KFD}, ' from CODE' ) if _debugging_details;

                        $op->open_pipe( $self->_debug_fd );
                        push @close_on_fail, $op->{KFD}, $op->{FD};

                        my $s = '';
                        $op->{KIN_REF} = \$s;
                    }
                    else {
                        croak( "'" . ref($source) . "' not allowed as a source for input redirection" );
                    }
                    $op->_init_filters;
                }
                elsif ( $op->{TYPE} eq '<pipe' ) {
                    _debug(
                        'kid to read ', $op->{KFD},
                        ' from a pipe IPC::Run opens and returns',
                    ) if _debugging_details;

                    my ( $r, $w ) = $op->open_pipe( $self->_debug_fd, $op->{SOURCE} );
                    _debug "caller will write to ", fileno $op->{SOURCE}
                      if _debugging_details;

                    $op->{TFD} = $r;
                    $op->{FD}  = undef;    # we don't manage this fd
                    $op->_init_filters;
                }
                elsif ( $op->{TYPE} eq '<pty<' ) {
                    _debug(
                        'kid to read ', $op->{KFD}, " from pty '", $op->{PTY_ID}, "'",
                    ) if _debugging_details;

                    for my $source ( $op->{SOURCE} ) {
                        if ( UNIVERSAL::isa( $source, 'SCALAR' ) ) {
                            _debug(
                                "kid ",                   $kid->{NUM},   " to read ", $op->{KFD},
                                " from SCALAR via pty '", $op->{PTY_ID}, "'"
                            ) if _debugging_details;

                            my $s = '';
                            $op->{KIN_REF} = \$s;
                        }
                        elsif ( UNIVERSAL::isa( $source, 'CODE' ) ) {
                            _debug(
                                "kid ",                 $kid->{NUM},   " to read ", $op->{KFD},
                                " from CODE via pty '", $op->{PTY_ID}, "'"
                            ) if _debugging_details;
                            my $s = '';
                            $op->{KIN_REF} = \$s;
                        }
                        else {
                            croak( "'" . ref($source) . "' not allowed as a source for '<pty<' redirection" );
                        }
                    }
                    $op->{FD}  = $self->{PTYS}->{ $op->{PTY_ID} }->fileno;
                    $op->{TFD} = undef;                                      # The fd isn't known until after fork().
                    $op->_init_filters;
                }
                elsif ( $op->{TYPE} eq '>' ) {
                    ## N> output redirection.
                    my $dest = $op->{DEST};
                    if ( !ref $dest ) {
                        _debug(
                            "kid ",  $kid->{NUM}, " to write ", $op->{KFD},
                            " to '", $dest,       "' (write only, create, ",
                            ( $op->{TRUNC} ? 'truncate' : 'append' ),
                            ")"
                        ) if _debugging_details;
                        croak "simulated open failure"
                          if $self->{_simulate_open_failure};
                        $op->{TFD} = _sysopen(
                            $dest,
                            ( O_WRONLY | O_CREAT | ( $op->{TRUNC} ? O_TRUNC : O_APPEND ) )
                        );
                        if (Win32_MODE) {
                            ## I have no idea why this is needed to make the current
                            ## file position survive the gyrations TFD must go
                            ## through...
                            POSIX::lseek( $op->{TFD}, 0, POSIX::SEEK_END() );
                        }
                        push @close_on_fail, $op->{TFD};
                    }
                    elsif ( UNIVERSAL::isa( $dest, 'GLOB' ) ) {
                        croak("Unopened filehandle in output redirect, command $kid->{NUM}") unless defined fileno $dest;
                        ## Turn on autoflush, mostly just to flush out
                        ## existing output.
                        my $old_fh = select($dest);
                        $| = 1;
                        select($old_fh);
                        $op->{TFD} = fileno $dest;
                        _debug( 'kid to write ', $op->{KFD}, ' to handle ', $op->{TFD} ) if _debugging_details;
                    }
                    elsif ( UNIVERSAL::isa( $dest, 'SCALAR' ) ) {
                        _debug( "kid ", $kid->{NUM}, " to write $op->{KFD} to SCALAR" ) if _debugging_details;

                        $op->open_pipe( $self->_debug_fd );
                        push @close_on_fail, $op->{FD}, $op->{TFD};
                        $$dest = '' if $op->{TRUNC};
                    }
                    elsif ( UNIVERSAL::isa( $dest, 'CODE' ) ) {
                        _debug("kid $kid->{NUM} to write $op->{KFD} to CODE") if _debugging_details;

                        $op->open_pipe( $self->_debug_fd );
                        push @close_on_fail, $op->{FD}, $op->{TFD};
                    }
                    else {
                        croak( "'" . ref($dest) . "' not allowed as a sink for output redirection" );
                    }
                    $output_fds_accum[ $op->{KFD} ] = $op;
                    $op->_init_filters;
                }

                elsif ( $op->{TYPE} eq '>pipe' ) {
                    ## N> output redirection to a pipe we open, but don't select()
                    ## on.
                    _debug(
                        "kid ", $kid->{NUM}, " to write ", $op->{KFD},
                        ' to a pipe IPC::Run opens and returns'
                    ) if _debugging_details;

                    my ( $r, $w ) = $op->open_pipe( $self->_debug_fd, $op->{DEST} );
                    _debug "caller will read from ", fileno $op->{DEST}
                      if _debugging_details;

                    $op->{TFD} = $w;
                    $op->{FD}  = undef;    # we don't manage this fd
                    $op->_init_filters;

                    $output_fds_accum[ $op->{KFD} ] = $op;
                }
                elsif ( $op->{TYPE} eq '>pty>' ) {
                    my $dest = $op->{DEST};
                    if ( UNIVERSAL::isa( $dest, 'SCALAR' ) ) {
                        _debug(
                            "kid ",                 $kid->{NUM},   " to write ", $op->{KFD},
                            " to SCALAR via pty '", $op->{PTY_ID}, "'"
                        ) if _debugging_details;

                        $$dest = '' if $op->{TRUNC};
                    }
                    elsif ( UNIVERSAL::isa( $dest, 'CODE' ) ) {
                        _debug(
                            "kid ",               $kid->{NUM},   " to write ", $op->{KFD},
                            " to CODE via pty '", $op->{PTY_ID}, "'"
                        ) if _debugging_details;
                    }
                    else {
                        croak( "'" . ref($dest) . "' not allowed as a sink for output redirection" );
                    }

                    $op->{FD}                       = $self->{PTYS}->{ $op->{PTY_ID} }->fileno;
                    $op->{TFD}                      = undef;                                      # The fd isn't known until after fork().
                    $output_fds_accum[ $op->{KFD} ] = $op;
                    $op->_init_filters;
                }
                elsif ( $op->{TYPE} eq '|' ) {
                    _debug( "pipelining $kid->{NUM} and " . ( $kid->{NUM} + 1 ) ) if _debugging_details;
                    ( $pipe_read_fd, $op->{TFD} ) = _pipe;
                    if (Win32_MODE) {
                        _dont_inherit($pipe_read_fd);
                        _dont_inherit( $op->{TFD} );
                    }
                    @output_fds_accum = ();
                }
                elsif ( $op->{TYPE} eq '&' ) {
                    @output_fds_accum = ();
                }    # end if $op->{TYPE} tree
                1;
            };    # end eval
            unless ($ok) {
                push @errs, $@;
                _debug 'caught ', $@ if _debugging;
            }
        }    # end for ( OPS }
    }

    if (@errs) {
        for (@close_on_fail) {
            _close($_);
            $_ = undef;
        }
        for ( keys %{ $self->{PTYS} } ) {
            next unless $self->{PTYS}->{$_};
            close $self->{PTYS}->{$_};
            $self->{PTYS}->{$_} = undef;
        }
        die join( '', @errs );
    }

    ## give all but the last child all of the output file descriptors
    ## These will be reopened (and thus rendered useless) if the child
    ## dup2s on to these descriptors, since we unshift these.  This way
    ## each process emits output to the same file descriptors that the
    ## last child will write to.  This is probably not quite correct,
    ## since each child should write to the file descriptors inherited
    ## from the parent.
    ## TODO: fix the inheritance of output file descriptors.
    ## NOTE: This sharing of OPS among kids means that we can't easily put
    ## a kid number in each OPS structure to ping the kid when all ops
    ## have closed (when $self->{PIPES} has emptied).  This means that we
    ## need to scan the KIDS whenever @{$self->{PIPES}} is empty to see
    ## if there any of them are still alive.
    for ( my $num = 0; $num < $#{ $self->{KIDS} }; ++$num ) {
        for ( reverse @output_fds_accum ) {
            next unless defined $_;
            _debug(
                'kid ', $self->{KIDS}->[$num]->{NUM}, ' also to write ', $_->{KFD},
                ' to ', ref $_->{DEST}
            ) if _debugging_details;
            unshift @{ $self->{KIDS}->[$num]->{OPS} }, $_;
        }
    }

    ## Open the debug pipe if we need it
    ## Create the list of PIPES we need to scan and the bit vectors needed by
    ## select().  Do this first so that _cleanup can _clobber() them if an
    ## exception occurs.
    @{ $self->{PIPES} } = ();
    $self->{RIN} = '';
    $self->{WIN} = '';
    $self->{EIN} = '';
    ## PIN is a vec()tor that indicates who's paused.
    $self->{PIN} = '';
    for my $kid ( @{ $self->{KIDS} } ) {
        for ( @{ $kid->{OPS} } ) {
            if ( defined $_->{FD} ) {
                _debug(
                    'kid ',    $kid->{NUM}, '[', $kid->{PID}, "]'s ", $_->{KFD},
                    ' is my ', $_->{FD}
                ) if _debugging_details;
                vec( $self->{ $_->{TYPE} =~ /^</ ? 'WIN' : 'RIN' }, $_->{FD}, 1 ) = 1;

                #	    vec( $self->{EIN}, $_->{FD}, 1 ) = 1;
                push @{ $self->{PIPES} }, $_;
            }
        }
    }

    for my $io ( @{ $self->{IOS} } ) {
        my $fd = $io->fileno;
        vec( $self->{RIN}, $fd, 1 ) = 1 if $io->mode =~ /r/;
        vec( $self->{WIN}, $fd, 1 ) = 1 if $io->mode =~ /w/;

        #      vec( $self->{EIN}, $fd, 1 ) = 1;
        push @{ $self->{PIPES} }, $io;
    }

    ## Put filters on the end of the filter chains to read & write the pipes.
    ## Clear pipe states
    for my $pipe ( @{ $self->{PIPES} } ) {
        $pipe->{SOURCE_EMPTY} = 0;
        $pipe->{PAUSED}       = 0;
        if ( $pipe->{TYPE} =~ /^>/ ) {
            my $pipe_reader = sub {
                my ( undef, $out_ref ) = @_;

                return undef unless defined $pipe->{FD};
                return 0 unless vec( $self->{ROUT}, $pipe->{FD}, 1 );

                vec( $self->{ROUT}, $pipe->{FD}, 1 ) = 0;

                _debug_desc_fd( 'reading from', $pipe ) if _debugging_details;
                my $in = eval { _read( $pipe->{FD} ) };
                if ($@) {
                    $in = '';
                    ## IO::Pty throws the Input/output error if the kid dies.
                    ## read() throws the bad file descriptor message if the
                    ## kid dies on Win32.
                    die $@
                      unless $@ =~ $_EIO
                      || ( $@ =~ /input or output/ && $^O =~ /aix/ )
                      || ( Win32_MODE && $@ =~ /Bad file descriptor/ );
                }

                unless ( length $in ) {
                    $self->_clobber($pipe);
                    return undef;
                }

                ## Protect the position so /.../g matches may be used.
                my $pos = pos $$out_ref;
                $$out_ref .= $in;
                pos($$out_ref) = $pos;
                return 1;
            };
            ## Input filters are the last filters
            push @{ $pipe->{FILTERS} },      $pipe_reader;
            push @{ $self->{TEMP_FILTERS} }, $pipe_reader;
        }
        else {
            my $pipe_writer = sub {
                my ( $in_ref, $out_ref ) = @_;
                return undef unless defined $pipe->{FD};
                return 0
                  unless vec( $self->{WOUT}, $pipe->{FD}, 1 )
                  || $pipe->{PAUSED};

                vec( $self->{WOUT}, $pipe->{FD}, 1 ) = 0;

                if ( !length $$in_ref ) {
                    if ( !defined get_more_input ) {
                        $self->_clobber($pipe);
                        return undef;
                    }
                }

                unless ( length $$in_ref ) {
                    unless ( $pipe->{PAUSED} ) {
                        _debug_desc_fd( 'pausing', $pipe ) if _debugging_details;
                        vec( $self->{WIN}, $pipe->{FD}, 1 ) = 0;

                        #		  vec( $self->{EIN}, $pipe->{FD}, 1 ) = 0;
                        vec( $self->{PIN}, $pipe->{FD}, 1 ) = 1;
                        $pipe->{PAUSED} = 1;
                    }
                    return 0;
                }
                _debug_desc_fd( 'writing to', $pipe ) if _debugging_details;

                my $c = _write( $pipe->{FD}, $$in_ref );
                substr( $$in_ref, 0, $c, '' );
                return 1;
            };
            ## Output filters are the first filters
            unshift @{ $pipe->{FILTERS} }, $pipe_writer;
            push @{ $self->{TEMP_FILTERS} }, $pipe_writer;
        }
    }
}

sub _dup2_gently {
    ## A METHOD, NOT A FUNCTION, NEEDS $self!
    my IPC::Run $self = shift;
    my ( $files, $fd1, $fd2 ) = @_;
    ## Moves TFDs that are using the destination fd out of the
    ## way before calling _dup2
    for (@$files) {
        next unless defined $_->{TFD};
        $_->{TFD} = _dup( $_->{TFD} ) if $_->{TFD} == $fd2;
    }
    $self->{DEBUG_FD} = _dup $self->{DEBUG_FD}
      if defined $self->{DEBUG_FD} && $self->{DEBUG_FD} == $fd2;

    _dup2_rudely( $fd1, $fd2 );
}

=pod

=item close_terminal

This is used as (or in) an init sub to cast off the bonds of a controlling
terminal.  It must precede all other redirection ops that affect
STDIN, STDOUT, or STDERR to be guaranteed effective.

=cut

sub close_terminal {
    ## Cast of the bonds of a controlling terminal

    POSIX::setsid() || croak "POSIX::setsid() failed";
    _debug "closing stdin, out, err"
      if _debugging_details;
    close STDIN;
    close STDERR;
    close STDOUT;
}

sub _do_kid_and_exit {
    my IPC::Run $self = shift;
    my ($kid) = @_;

    my ( $s1, $s2 );
    if ( $] < 5.008 ) {
        ## For unknown reasons, placing these two statements in the eval{}
        ## causes the eval {} to not catch errors after they are executed in
        ## perl 5.6.0, godforsaken version that it is...not sure about 5.6.1.
        ## Part of this could be that these symbols get destructed when
        ## exiting the eval, and that destruction might be what's (wrongly)
        ## confusing the eval{}, allowing the exception to propagate.
        $s1 = Symbol::gensym();
        $s2 = Symbol::gensym();
    }

    eval {
        local $cur_self = $self;

        if (_debugging) {
            _set_child_debug_name(
                ref $kid->{VAL} eq "CODE"
                ? "CODE"
                : basename( $kid->{VAL}->[0] )
            );
        }

        ## close parent FD's first so they're out of the way.
        ## Don't close STDIN, STDOUT, STDERR: they should be inherited or
        ## overwritten below.
        my @needed = $self->{noinherit} ? () : ( 1, 1, 1 );
        $needed[ $self->{SYNC_WRITER_FD} ] = 1;
        $needed[ $self->{DEBUG_FD} ] = 1 if defined $self->{DEBUG_FD};

        for ( @{ $kid->{OPS} } ) {
            $needed[ $_->{TFD} ] = 1 if defined $_->{TFD};
        }

        ## TODO: use the forthcoming IO::Pty to close the terminal and
        ## make the first pty for this child the controlling terminal.
        ## This will also make it so that pty-laden kids don't cause
        ## other kids to lose stdin/stdout/stderr.
        my @closed;
        if ( %{ $self->{PTYS} } ) {
            ## Clean up the parent's fds.
            for ( keys %{ $self->{PTYS} } ) {
                _debug "Cleaning up parent's ptty '$_'" if _debugging_details;
                my $slave = $self->{PTYS}->{$_}->slave;
                $closed[ $self->{PTYS}->{$_}->fileno ] = 1;
                close $self->{PTYS}->{$_};
                $self->{PTYS}->{$_} = $slave;
            }

            close_terminal;
            $closed[$_] = 1 for ( 0 .. 2 );
        }

        for my $sibling ( @{ $self->{KIDS} } ) {
            for ( @{ $sibling->{OPS} } ) {
                if ( $_->{TYPE} =~ /^.pty.$/ ) {
                    $_->{TFD} = $self->{PTYS}->{ $_->{PTY_ID} }->fileno;
                    $needed[ $_->{TFD} ] = 1;
                }

                #	    for ( $_->{FD}, ( $sibling != $kid ? $_->{TFD} : () ) ) {
                #	       if ( defined $_ && ! $closed[$_] && ! $needed[$_] ) {
                #		  _close( $_ );
                #		  $closed[$_] = 1;
                #		  $_ = undef;
                #	       }
                #	    }
            }
        }

        ## This is crude: we have no way of keeping track of browsing all open
        ## fds, so we scan to a fairly high fd.
        _debug "open fds: ", join " ", keys %fds if _debugging_details;
        for ( keys %fds ) {
            if ( !$closed[$_] && !$needed[$_] ) {
                _close($_);
                $closed[$_] = 1;
            }
        }

        ## Lazy closing is so the same fd (ie the same TFD value) can be dup2'ed on
        ## several times.
        my @lazy_close;
        for ( @{ $kid->{OPS} } ) {
            if ( defined $_->{TFD} ) {
                unless ( $_->{TFD} == $_->{KFD} ) {
                    $self->_dup2_gently( $kid->{OPS}, $_->{TFD}, $_->{KFD} );
                    push @lazy_close, $_->{TFD};
                }
            }
            elsif ( $_->{TYPE} eq 'dup' ) {
                $self->_dup2_gently( $kid->{OPS}, $_->{KFD1}, $_->{KFD2} )
                  unless $_->{KFD1} == $_->{KFD2};
            }
            elsif ( $_->{TYPE} eq 'close' ) {
                for ( $_->{KFD} ) {
                    if ( !$closed[$_] ) {
                        _close($_);
                        $closed[$_] = 1;
                        $_ = undef;
                    }
                }
            }
            elsif ( $_->{TYPE} eq 'init' ) {
                $_->{SUB}->();
            }
        }

        for (@lazy_close) {
            unless ( $closed[$_] ) {
                _close($_);
                $closed[$_] = 1;
            }
        }

        if ( ref $kid->{VAL} ne 'CODE' ) {
            open $s1, ">&=$self->{SYNC_WRITER_FD}"
              or croak "$! setting filehandle to fd SYNC_WRITER_FD";
            fcntl $s1, F_SETFD, 1;

            if ( defined $self->{DEBUG_FD} ) {
                open $s2, ">&=$self->{DEBUG_FD}"
                  or croak "$! setting filehandle to fd DEBUG_FD";
                fcntl $s2, F_SETFD, 1;
            }

            if (_debugging) {
                my @cmd = ( $kid->{PATH}, @{ $kid->{VAL} }[ 1 .. $#{ $kid->{VAL} } ] );
                _debug 'execing ', join " ", map { /[\s\"]/ ? "'$_'" : $_ } @cmd;
            }

            die "exec failed: simulating exec() failure"
              if $self->{_simulate_exec_failure};

            _exec $kid->{PATH}, @{ $kid->{VAL} }[ 1 .. $#{ $kid->{VAL} } ];

            croak "exec failed: $!";
        }
    };
    if ($@) {
        _write $self->{SYNC_WRITER_FD}, $@;
        ## Avoid DESTROY.
        POSIX::exit 1;
    }

    ## We must be executing code in the child, otherwise exec() would have
    ## prevented us from being here.
    _close $self->{SYNC_WRITER_FD};
    _debug 'calling fork()ed CODE ref' if _debugging;
    POSIX::close $self->{DEBUG_FD} if defined $self->{DEBUG_FD};
    ## TODO: Overload CORE::GLOBAL::exit...
    $kid->{VAL}->();

    ## There are bugs in perl closures up to and including 5.6.1
    ## that may keep this next line from having any effect, and it
    ## won't have any effect if our caller has kept a copy of it, but
    ## this may cause the closure to be cleaned up.  Maybe.
    $kid->{VAL} = undef;

    ## Use POSIX::exit to avoid global destruction, since this might
    ## cause DESTROY() to be called on objects created in the parent
    ## and thus cause double cleanup.  For instance, if DESTROY() unlinks
    ## a file in the child, we don't want the parent to suddenly miss
    ## it.
    POSIX::exit 0;
}

=pod

=item start

   $h = start(
      \@cmd, \$in, \$out, ...,
      timeout( 30, name => "process timeout" ),
      $stall_timeout = timeout( 10, name => "stall timeout"   ),
   );

   $h = start \@cmd, '<', \$in, '|', \@cmd2, ...;

start() accepts a harness or harness specification and returns a harness
after building all of the pipes and launching (via fork()/exec(), or, maybe
someday, spawn()) all the child processes.  It does not send or receive any
data on the pipes, see pump() and finish() for that.

You may call harness() and then pass it's result to start() if you like,
but you only need to if it helps you structure or tune your application.
If you do call harness(), you may skip start() and proceed directly to
pump.

start() also starts all timers in the harness.  See L<IPC::Run::Timer>
for more information.

start() flushes STDOUT and STDERR to help you avoid duplicate output.
It has no way of asking Perl to flush all your open filehandles, so
you are going to need to flush any others you have open.  Sorry.

Here's how if you don't want to alter the state of $| for your
filehandle:

   $ofh = select HANDLE; $of = $|; $| = 1; $| = $of; select $ofh;

If you don't mind leaving output unbuffered on HANDLE, you can do
the slightly shorter

   $ofh = select HANDLE; $| = 1; select $ofh;

Or, you can use IO::Handle's flush() method:

   use IO::Handle;
   flush HANDLE;

Perl needs the equivalent of C's fflush( (FILE *)NULL ).

=cut

sub start {

    # $SIG{__DIE__} = sub { my $s = shift; Carp::cluck $s; die $s };
    my $options;
    if ( @_ && ref $_[-1] eq 'HASH' ) {
        $options = pop;
        require Data::Dumper;
        carp "Passing in options as a hash is deprecated:\n", Data::Dumper::Dumper($options);
    }

    my IPC::Run $self;
    if ( @_ == 1 && UNIVERSAL::isa( $_[0], __PACKAGE__ ) ) {
        $self = shift;
        $self->{$_} = $options->{$_} for keys %$options;
    }
    else {
        $self = harness( @_, $options ? $options : () );
    }

    local $cur_self = $self;

    $self->kill_kill if $self->{STATE} == _started;

    _debug "** starting" if _debugging;

    $_->{RESULT} = undef for @{ $self->{KIDS} };

    ## Assume we're not being called from &run.  It will correct our
    ## assumption if need be.  This affects whether &_select_loop clears
    ## input queues to '' when they're empty.
    $self->{clear_ins} = 1;

    IPC::Run::Win32Helper::optimize $self
      if Win32_MODE && $in_run;

    my @errs;

    for ( @{ $self->{TIMERS} } ) {
        eval { $_->start };
        if ($@) {
            push @errs, $@;
            _debug 'caught ', $@ if _debugging;
        }
    }

    eval { $self->_open_pipes };
    if ($@) {
        push @errs, $@;
        _debug 'caught ', $@ if _debugging;
    }

    if ( !@errs ) {
        ## This is a bit of a hack, we should do it for all open filehandles.
        ## Since there's no way I know of to enumerate open filehandles, we
        ## autoflush STDOUT and STDERR.  This is done so that the children don't
        ## inherit output buffers chock full o' redundant data.  It's really
        ## confusing to track that down.
        { my $ofh = select STDOUT; my $of = $|; $| = 1; $| = $of; select $ofh; }
        { my $ofh = select STDERR; my $of = $|; $| = 1; $| = $of; select $ofh; }
        for my $kid ( @{ $self->{KIDS} } ) {
            $kid->{RESULT} = undef;
            _debug "child: ",
              ref( $kid->{VAL} ) eq "CODE"
              ? "CODE ref"
              : (
                "`",
                join( " ", map /[^\w.-]/ ? "'$_'" : $_, @{ $kid->{VAL} } ),
                "`"
              ) if _debugging_details;
            eval {
                croak "simulated failure of fork"
                  if $self->{_simulate_fork_failure};
                unless (Win32_MODE) {
                    $self->_spawn($kid);
                }
                else {
## TODO: Test and debug spawning code.  Someday.
                    _debug(
                        'spawning ',
                        join(
                            ' ',
                            map( "'$_'",
                                ( $kid->{PATH}, @{ $kid->{VAL} }[ 1 .. $#{ $kid->{VAL} } ] ) )
                        )
                    ) if _debugging;
                    ## The external kid wouldn't know what to do with it anyway.
                    ## This is only used by the "helper" pump processes on Win32.
                    _dont_inherit( $self->{DEBUG_FD} );
                    ( $kid->{PID}, $kid->{PROCESS} ) = IPC::Run::Win32Helper::win32_spawn(
                        [ $kid->{PATH}, @{ $kid->{VAL} }[ 1 .. $#{ $kid->{VAL} } ] ],
                        $kid->{OPS},
                    );
                    _debug "spawn() = ", $kid->{PID} if _debugging;
                }
            };
            if ($@) {
                push @errs, $@;
                _debug 'caught ', $@ if _debugging;
            }
        }
    }

    ## Close all those temporary filehandles that the kids needed.
    for my $pty ( values %{ $self->{PTYS} } ) {
        close $pty->slave;
    }

    my @closed;
    for my $kid ( @{ $self->{KIDS} } ) {
        for ( @{ $kid->{OPS} } ) {
            my $close_it = eval {
                     defined $_->{TFD}
                  && !$_->{DONT_CLOSE}
                  && !$closed[ $_->{TFD} ]
                  && ( !Win32_MODE || !$_->{RECV_THROUGH_TEMP_FILE} )    ## Win32 hack
            };
            if ($@) {
                push @errs, $@;
                _debug 'caught ', $@ if _debugging;
            }
            if ( $close_it || $@ ) {
                eval {
                    _close( $_->{TFD} );
                    $closed[ $_->{TFD} ] = 1;
                    $_->{TFD} = undef;
                };
                if ($@) {
                    push @errs, $@;
                    _debug 'caught ', $@ if _debugging;
                }
            }
        }
    }
    confess "gak!" unless defined $self->{PIPES};

    if (@errs) {
        eval { $self->_cleanup };
        warn $@ if $@;
        die join( '', @errs );
    }

    $self->{STATE} = _started;
    return $self;
}

=item adopt

Experimental feature. NOT FUNCTIONAL YET, NEED TO CLOSE FDS BETTER IN CHILDREN.  SEE t/adopt.t for a test suite.

=cut

sub adopt {
    my IPC::Run $self = shift;

    for my $adoptee (@_) {
        push @{ $self->{IOS} }, @{ $adoptee->{IOS} };
        ## NEED TO RENUMBER THE KIDS!!
        push @{ $self->{KIDS} },  @{ $adoptee->{KIDS} };
        push @{ $self->{PIPES} }, @{ $adoptee->{PIPES} };
        $self->{PTYS}->{$_} = $adoptee->{PTYS}->{$_} for keys %{ $adoptee->{PYTS} };
        push @{ $self->{TIMERS} }, @{ $adoptee->{TIMERS} };
        $adoptee->{STATE} = _finished;
    }
}

sub _clobber {
    my IPC::Run $self = shift;
    my ($file) = @_;
    _debug_desc_fd( "closing", $file ) if _debugging_details;
    my $doomed = $file->{FD};
    my $dir = $file->{TYPE} =~ /^</ ? 'WIN' : 'RIN';
    vec( $self->{$dir}, $doomed, 1 ) = 0;

    #   vec( $self->{EIN},  $doomed, 1 ) = 0;
    vec( $self->{PIN}, $doomed, 1 ) = 0;
    if ( $file->{TYPE} =~ /^(.)pty.$/ ) {
        if ( $1 eq '>' ) {
            ## Only close output ptys.  This is so that ptys as inputs are
            ## never autoclosed, which would risk losing data that was
            ## in the slave->parent queue.
            _debug_desc_fd "closing pty", $file if _debugging_details;
            close $self->{PTYS}->{ $file->{PTY_ID} }
              if defined $self->{PTYS}->{ $file->{PTY_ID} };
            $self->{PTYS}->{ $file->{PTY_ID} } = undef;
        }
    }
    elsif ( UNIVERSAL::isa( $file, 'IPC::Run::IO' ) ) {
        $file->close unless $file->{DONT_CLOSE};
    }
    else {
        _close($doomed);
    }

    @{ $self->{PIPES} } = grep
      defined $_->{FD} && ( $_->{TYPE} ne $file->{TYPE} || $_->{FD} ne $doomed ),
      @{ $self->{PIPES} };

    $file->{FD} = undef;
}

sub _select_loop {
    my IPC::Run $self = shift;

    my $io_occurred;

    my $not_forever = 0.01;

  SELECT:
    while ( $self->pumpable ) {
        if ( $io_occurred && $self->{break_on_io} ) {
            _debug "exiting _select(): io occurred and break_on_io set"
              if _debugging_details;
            last;
        }

        my $timeout = $self->{non_blocking} ? 0 : undef;

        if ( @{ $self->{TIMERS} } ) {
            my $now = time;
            my $time_left;
            for ( @{ $self->{TIMERS} } ) {
                next unless $_->is_running;
                $time_left = $_->check($now);
                ## Return when a timer expires
                return if defined $time_left && !$time_left;
                $timeout = $time_left
                  if !defined $timeout || $time_left < $timeout;
            }
        }

        ##
        ## See if we can unpause any input channels
        ##
        my $paused = 0;

        for my $file ( @{ $self->{PIPES} } ) {
            next unless $file->{PAUSED} && $file->{TYPE} =~ /^</;

            _debug_desc_fd( "checking for more input", $file ) if _debugging_details;
            my $did;
            1 while $did = $file->_do_filters($self);
            if ( defined $file->{FD} && !defined($did) || $did ) {
                _debug_desc_fd( "unpausing", $file ) if _debugging_details;
                $file->{PAUSED} = 0;
                vec( $self->{WIN}, $file->{FD}, 1 ) = 1;

                #	    vec( $self->{EIN}, $file->{FD}, 1 ) = 1;
                vec( $self->{PIN}, $file->{FD}, 1 ) = 0;
            }
            else {
                ## This gets incremented occasionally when the IO channel
                ## was actually closed.  That's a bug, but it seems mostly
                ## harmless: it causes us to exit if break_on_io, or to set
                ## the timeout to not be forever.  I need to fix it, though.
                ++$paused;
            }
        }

        if (_debugging_details) {
            my $map = join(
                '',
                map {
                    my $out;
                    $out = 'r' if vec( $self->{RIN}, $_, 1 );
                    $out = $out ? 'b' : 'w' if vec( $self->{WIN}, $_, 1 );
                    $out = 'p' if !$out && vec( $self->{PIN}, $_, 1 );
                    $out = $out ? uc($out) : 'x' if vec( $self->{EIN}, $_, 1 );
                    $out = '-' unless $out;
                    $out;
                } ( 0 .. 1024 )
            );
            $map =~ s/((?:[a-zA-Z-]|\([^\)]*\)){12,}?)-*$/$1/;
            _debug 'fds for select: ', $map if _debugging_details;
        }

        ## _do_filters may have closed our last fd, and we need to see if
        ## we have I/O, or are just waiting for children to exit.
        my $p = $self->pumpable;
        last unless $p;
        if ( $p != 0 && ( !defined $timeout || $timeout > 0.1 ) ) {
            ## No I/O will wake the select loop up, but we have children
            ## lingering, so we need to poll them with a short timeout.
            ## Otherwise, assume more input will be coming.
            $timeout = $not_forever;
            $not_forever *= 2;
            $not_forever = 0.5 if $not_forever >= 0.5;
        }

        ## Make sure we don't block forever in select() because inputs are
        ## paused.
        if ( !defined $timeout && !( @{ $self->{PIPES} } - $paused ) ) {
            ## Need to return if we're in pump and all input is paused, or
            ## we'll loop until all inputs are unpaused, which is darn near
            ## forever.  And a day.
            if ( $self->{break_on_io} ) {
                _debug "exiting _select(): no I/O to do and timeout=forever"
                  if _debugging;
                last;
            }

            ## Otherwise, assume more input will be coming.
            $timeout = $not_forever;
            $not_forever *= 2;
            $not_forever = 0.5 if $not_forever >= 0.5;
        }

        _debug 'timeout=', defined $timeout ? $timeout : 'forever'
          if _debugging_details;

        my $nfound;
        unless (Win32_MODE) {
            $nfound = select(
                $self->{ROUT} = $self->{RIN},
                $self->{WOUT} = $self->{WIN},
                $self->{EOUT} = $self->{EIN},
                $timeout
            );
        }
        else {
            my @in = map $self->{$_}, qw( RIN WIN EIN );
            ## Win32's select() on Win32 seems to die if passed vectors of
            ## all 0's.  Need to report this when I get back online.
            for (@in) {
                $_ = undef unless index( ( unpack "b*", $_ ), 1 ) >= 0;
            }

            $nfound = select(
                $self->{ROUT} = $in[0],
                $self->{WOUT} = $in[1],
                $self->{EOUT} = $in[2],
                $timeout
            );

            for ( $self->{ROUT}, $self->{WOUT}, $self->{EOUT} ) {
                $_ = "" unless defined $_;
            }
        }
        last if !$nfound && $self->{non_blocking};

        if ( $nfound < 0 ) {
            if ( $! == POSIX::EINTR() ) {

                # Caught a signal before any FD went ready.  Ensure that
                # the bit fields reflect "no FDs ready".
                $self->{ROUT} = $self->{WOUT} = $self->{EOUT} = '';
                $nfound = 0;
            }
            else {
                croak "$! in select";
            }
        }
        ## TODO: Analyze the EINTR failure mode and see if this patch
        ## is adequate and optimal.
        ## TODO: Add an EINTR test to the test suite.

        if (_debugging_details) {
            my $map = join(
                '',
                map {
                    my $out;
                    $out = 'r' if vec( $self->{ROUT}, $_, 1 );
                    $out = $out ? 'b'      : 'w' if vec( $self->{WOUT}, $_, 1 );
                    $out = $out ? uc($out) : 'x' if vec( $self->{EOUT}, $_, 1 );
                    $out = '-' unless $out;
                    $out;
                } ( 0 .. 128 )
            );
            $map =~ s/((?:[a-zA-Z-]|\([^\)]*\)){12,}?)-*$/$1/;
            _debug "selected  ", $map;
        }

        ## Need to copy since _clobber alters @{$self->{PIPES}}.
        ## TODO: Rethink _clobber().  Rethink $file->{PAUSED}, too.
        my @pipes = @{ $self->{PIPES} };
        $io_occurred = $_->poll($self) ? 1 : $io_occurred for @pipes;

        #   FILE:
        #      for my $pipe ( @pipes ) {
        #         ## Pipes can be shared among kids.  If another kid closes the
        #         ## pipe, then it's {FD} will be undef.  Also, on Win32, pipes can
        #	 ## be optimized to be files, in which case the FD is left undef
        #	 ## so we don't try to select() on it.
        #         if ( $pipe->{TYPE} =~ /^>/
        #            && defined $pipe->{FD}
        #            && vec( $self->{ROUT}, $pipe->{FD}, 1 )
        #         ) {
        #            _debug_desc_fd( "filtering data from", $pipe ) if _debugging_details;
        #confess "phooey" unless UNIVERSAL::isa( $pipe, "IPC::Run::IO" );
        #            $io_occurred = 1 if $pipe->_do_filters( $self );
        #
        #            next FILE unless defined $pipe->{FD};
        #         }
        #
        #	 ## On Win32, pipes to the child can be optimized to be files
        #	 ## and FD left undefined so we won't select on it.
        #         if ( $pipe->{TYPE} =~ /^</
        #            && defined $pipe->{FD}
        #            && vec( $self->{WOUT}, $pipe->{FD}, 1 )
        #         ) {
        #            _debug_desc_fd( "filtering data to", $pipe ) if _debugging_details;
        #            $io_occurred = 1 if $pipe->_do_filters( $self );
        #
        #            next FILE unless defined $pipe->{FD};
        #         }
        #
        #         if ( defined $pipe->{FD} && vec( $self->{EOUT}, $pipe->{FD}, 1 ) ) {
        #            ## BSD seems to sometimes raise the exceptional condition flag
        #            ## when a pipe is closed before we read it's last data.  This
        #            ## causes spurious warnings and generally renders the exception
        #            ## mechanism useless for our purposes.  The exception
        #            ## flag semantics are too variable (they're device driver
        #            ## specific) for me to easily map to any automatic action like
        #            ## warning or croaking (try running v0.42 if you don't believe me
        #            ## :-).
        #            warn "Exception on descriptor $pipe->{FD}";
        #         }
        #      }
    }

    return;
}

sub _cleanup {
    my IPC::Run $self = shift;
    _debug "cleaning up" if _debugging_details;

    for ( values %{ $self->{PTYS} } ) {
        next unless ref $_;
        eval {
            _debug "closing slave fd ", fileno $_->slave if _debugging_data;
            close $_->slave;
        };
        carp $@ . " while closing ptys" if $@;
        eval {
            _debug "closing master fd ", fileno $_ if _debugging_data;
            close $_;
        };
        carp $@ . " closing ptys" if $@;
    }

    _debug "cleaning up pipes" if _debugging_details;
    ## _clobber modifies PIPES
    $self->_clobber( $self->{PIPES}->[0] ) while @{ $self->{PIPES} };

    for my $kid ( @{ $self->{KIDS} } ) {
        _debug "cleaning up kid ", $kid->{NUM} if _debugging_details;
        if ( !length $kid->{PID} ) {
            _debug 'never ran child ', $kid->{NUM}, ", can't reap"
              if _debugging;
            for my $op ( @{ $kid->{OPS} } ) {
                _close( $op->{TFD} )
                  if defined $op->{TFD} && !defined $op->{TEMP_FILE_HANDLE};
            }
        }
        elsif ( !defined $kid->{RESULT} ) {
            _debug 'reaping child ', $kid->{NUM}, ' (pid ', $kid->{PID}, ')'
              if _debugging;
            my $pid = waitpid $kid->{PID}, 0;
            $kid->{RESULT} = $?;
            _debug 'reaped ', $pid, ', $?=', $kid->{RESULT}
              if _debugging;
        }

        #      if ( defined $kid->{DEBUG_FD} ) {
        #	 die;
        #         @{$kid->{OPS}} = grep
        #            ! defined $_->{KFD} || $_->{KFD} != $kid->{DEBUG_FD},
        #            @{$kid->{OPS}};
        #         $kid->{DEBUG_FD} = undef;
        #      }

        _debug "cleaning up filters" if _debugging_details;
        for my $op ( @{ $kid->{OPS} } ) {
            @{ $op->{FILTERS} } = grep {
                my $filter = $_;
                !grep $filter == $_, @{ $self->{TEMP_FILTERS} };
            } @{ $op->{FILTERS} };
        }

        for my $op ( @{ $kid->{OPS} } ) {
            $op->_cleanup($self) if UNIVERSAL::isa( $op, "IPC::Run::IO" );
        }
    }
    $self->{STATE} = _finished;
    @{ $self->{TEMP_FILTERS} } = ();
    _debug "done cleaning up" if _debugging_details;

    POSIX::close $self->{DEBUG_FD} if defined $self->{DEBUG_FD};
    $self->{DEBUG_FD} = undef;
}

=pod

=item pump

   pump $h;
   $h->pump;

Pump accepts a single parameter harness.  It blocks until it delivers some
input or receives some output.  It returns TRUE if there is still input or
output to be done, FALSE otherwise.

pump() will automatically call start() if need be, so you may call harness()
then proceed to pump() if that helps you structure your application.

If pump() is called after all harnessed activities have completed, a "process
ended prematurely" exception to be thrown.  This allows for simple scripting
of external applications without having to add lots of error handling code at
each step of the script:

   $h = harness \@smbclient, \$in, \$out, $err;

   $in = "cd /foo\n";
   $h->pump until $out =~ /^smb.*> \Z/m;
   die "error cding to /foo:\n$out" if $out =~ "ERR";
   $out = '';

   $in = "mget *\n";
   $h->pump until $out =~ /^smb.*> \Z/m;
   die "error retrieving files:\n$out" if $out =~ "ERR";

   $h->finish;

   warn $err if $err;

=cut

sub pump {
    die "pump() takes only a single harness as a parameter"
      unless @_ == 1 && UNIVERSAL::isa( $_[0], __PACKAGE__ );

    my IPC::Run $self = shift;

    local $cur_self = $self;

    _debug "** pumping"
      if _debugging;

    #   my $r = eval {
    $self->start if $self->{STATE} < _started;
    croak "process ended prematurely" unless $self->pumpable;

    $self->{auto_close_ins} = 0;
    $self->{break_on_io}    = 1;
    $self->_select_loop;
    return $self->pumpable;

    #   };
    #   if ( $@ ) {
    #      my $x = $@;
    #      _debug $x if _debugging && $x;
    #      eval { $self->_cleanup };
    #      warn $@ if $@;
    #      die $x;
    #   }
    #   return $r;
}

=pod

=item pump_nb

   pump_nb $h;
   $h->pump_nb;

"pump() non-blocking", pumps if anything's ready to be pumped, returns
immediately otherwise.  This is useful if you're doing some long-running
task in the foreground, but don't want to starve any child processes.

=cut

sub pump_nb {
    my IPC::Run $self = shift;

    $self->{non_blocking} = 1;
    my $r = eval { $self->pump };
    $self->{non_blocking} = 0;
    die $@ if $@;
    return $r;
}

=pod

=item pumpable

Returns TRUE if calling pump() won't throw an immediate "process ended
prematurely" exception.  This means that there are open I/O channels or
active processes. May yield the parent processes' time slice for 0.01
second if all pipes are to the child and all are paused.  In this case
we can't tell if the child is dead, so we yield the processor and
then attempt to reap the child in a nonblocking way.

=cut

## Undocumented feature (don't depend on it outside this module):
## returns -1 if we have I/O channels open, or >0 if no I/O channels
## open, but we have kids running.  This allows the select loop
## to poll for child exit.
sub pumpable {
    my IPC::Run $self = shift;

    ## There's a catch-22 we can get in to if there is only one pipe left
    ## open to the child and it's paused (ie the SCALAR it's tied to
    ## is '').  It's paused, so we're not select()ing on it, so we don't
    ## check it to see if the child attached to it is alive and it stays
    ## in @{$self->{PIPES}} forever.  So, if all pipes are paused, see if
    ## we can reap the child.
    return -1 if grep !$_->{PAUSED}, @{ $self->{PIPES} };

    ## See if the child is dead.
    $self->reap_nb;
    return 0 unless $self->_running_kids;

    ## If we reap_nb and it's not dead yet, yield to it to see if it
    ## exits.
    ##
    ## A better solution would be to unpause all the pipes, but I tried that
    ## and it never errored on linux.  Sigh.
    select undef, undef, undef, 0.0001;

    ## try again
    $self->reap_nb;
    return 0 unless $self->_running_kids;

    return -1;    ## There are pipes waiting
}

sub _running_kids {
    my IPC::Run $self = shift;
    return grep
      defined $_->{PID} && !defined $_->{RESULT},
      @{ $self->{KIDS} };
}

=pod

=item reap_nb

Attempts to reap child processes, but does not block.

Does not currently take any parameters, one day it will allow specific
children to be reaped.

Only call this from a signal handler if your C<perl> is recent enough
to have safe signal handling (5.6.1 did not, IIRC, but it was being discussed
on perl5-porters).  Calling this (or doing any significant work) in a signal
handler on older C<perl>s is asking for seg faults.

=cut

my $still_runnings;

sub reap_nb {
    my IPC::Run $self = shift;

    local $cur_self = $self;

    ## No more pipes, look to see if all the kids yet live, reaping those
    ## that haven't.  I'd use $SIG{CHLD}/$SIG{CLD}, but that's broken
    ## on older (SYSV) platforms and perhaps less portable than waitpid().
    ## This could be slow with a lot of kids, but that's rare and, well,
    ## a lot of kids is slow in the first place.
    ## Oh, and this keeps us from reaping other children the process
    ## may have spawned.
    for my $kid ( @{ $self->{KIDS} } ) {
        if (Win32_MODE) {
            next if !defined $kid->{PROCESS} || defined $kid->{RESULT};
            unless ( $kid->{PROCESS}->Wait(0) ) {
                _debug "kid $kid->{NUM} ($kid->{PID}) still running"
                  if _debugging_details;
                next;
            }

            _debug "kid $kid->{NUM} ($kid->{PID}) exited"
              if _debugging;

            $kid->{PROCESS}->GetExitCode( $kid->{RESULT} )
              or croak "$! while GetExitCode()ing for Win32 process";

            unless ( defined $kid->{RESULT} ) {
                $kid->{RESULT} = "0 but true";
                $? = $kid->{RESULT} = 0x0F;
            }
            else {
                $? = $kid->{RESULT} << 8;
            }
        }
        else {
            next if !defined $kid->{PID} || defined $kid->{RESULT};
            my $pid = waitpid $kid->{PID}, POSIX::WNOHANG();
            unless ($pid) {
                _debug "$kid->{NUM} ($kid->{PID}) still running"
                  if _debugging_details;
                next;
            }

            if ( $pid < 0 ) {
                _debug "No such process: $kid->{PID}\n" if _debugging;
                $kid->{RESULT} = "unknown result, unknown PID";
            }
            else {
                _debug "kid $kid->{NUM} ($kid->{PID}) exited"
                  if _debugging;

                confess "waitpid returned the wrong PID: $pid instead of $kid->{PID}"
                  unless $pid == $kid->{PID};
                _debug "$kid->{PID} returned $?\n" if _debugging;
                $kid->{RESULT} = $?;
            }
        }
    }
}

=pod

=item finish

This must be called after the last start() or pump() call for a harness,
or your system will accumulate defunct processes and you may "leak"
file descriptors.

finish() returns TRUE if all children returned 0 (and were not signaled and did
not coredump, ie ! $?), and FALSE otherwise (this is like run(), and the
opposite of system()).

Once a harness has been finished, it may be run() or start()ed again,
including by pump()s auto-start.

If this throws an exception rather than a normal exit, the harness may
be left in an unstable state, it's best to kill the harness to get rid
of all the child processes, etc.

Specifically, if a timeout expires in finish(), finish() will not
kill all the children.  Call C<<$h->kill_kill>> in this case if you care.
This differs from the behavior of L</run>.

=cut

sub finish {
    my IPC::Run $self = shift;
    my $options = @_ && ref $_[-1] eq 'HASH' ? pop : {};

    local $cur_self = $self;

    _debug "** finishing" if _debugging;

    $self->{non_blocking}   = 0;
    $self->{auto_close_ins} = 1;
    $self->{break_on_io}    = 0;

    # We don't alter $self->{clear_ins}, start() and run() control it.

    while ( $self->pumpable ) {
        $self->_select_loop($options);
    }
    $self->_cleanup;

    return !$self->full_result;
}

=pod

=item result

   $h->result;

Returns the first non-zero result code (ie $? >> 8).  See L</full_result> to 
get the $? value for a child process.

To get the result of a particular child, do:

   $h->result( 0 );  # first child's $? >> 8
   $h->result( 1 );  # second child

or

   ($h->results)[0]
   ($h->results)[1]

Returns undef if no child processes were spawned and no child number was
specified.  Throws an exception if an out-of-range child number is passed.

=cut

sub _assert_finished {
    my IPC::Run $self = $_[0];

    croak "Harness not run" unless $self->{STATE} >= _finished;
    croak "Harness not finished running" unless $self->{STATE} == _finished;
}

sub result {
    &_assert_finished;
    my IPC::Run $self = shift;

    if (@_) {
        my ($which) = @_;
        croak(
            "Only ",
            scalar( @{ $self->{KIDS} } ),
            " child processes, no process $which"
        ) unless $which >= 0 && $which <= $#{ $self->{KIDS} };
        return $self->{KIDS}->[$which]->{RESULT} >> 8;
    }
    else {
        return undef unless @{ $self->{KIDS} };
        for ( @{ $self->{KIDS} } ) {
            return $_->{RESULT} >> 8 if $_->{RESULT} >> 8;
        }
    }
}

=pod

=item results

Returns a list of child exit values.  See L</full_results> if you want to
know if a signal killed the child.

Throws an exception if the harness is not in a finished state.
 
=cut

sub results {
    &_assert_finished;
    my IPC::Run $self = shift;

    # we add 0 here to stop warnings associated with "unknown result, unknown PID"
    return map { ( 0 + $_->{RESULT} ) >> 8 } @{ $self->{KIDS} };
}

=pod

=item full_result

   $h->full_result;

Returns the first non-zero $?.  See L</result> to get the first $? >> 8 
value for a child process.

To get the result of a particular child, do:

   $h->full_result( 0 );  # first child's $?
   $h->full_result( 1 );  # second child

or

   ($h->full_results)[0]
   ($h->full_results)[1]

Returns undef if no child processes were spawned and no child number was
specified.  Throws an exception if an out-of-range child number is passed.

=cut

sub full_result {
    goto &result if @_ > 1;
    &_assert_finished;

    my IPC::Run $self = shift;

    return undef unless @{ $self->{KIDS} };
    for ( @{ $self->{KIDS} } ) {
        return $_->{RESULT} if $_->{RESULT};
    }
}

=pod

=item full_results

Returns a list of child exit values as returned by C<wait>.  See L</results>
if you don't care about coredumps or signals.

Throws an exception if the harness is not in a finished state.
 
=cut

sub full_results {
    &_assert_finished;
    my IPC::Run $self = shift;

    croak "Harness not run" unless $self->{STATE} >= _finished;
    croak "Harness not finished running" unless $self->{STATE} == _finished;

    return map $_->{RESULT}, @{ $self->{KIDS} };
}

##
## Filter Scaffolding
##
use vars (
    '$filter_op',     ## The op running a filter chain right now
    '$filter_num',    ## Which filter is being run right now.
);

##
## A few filters and filter constructors
##

=pod

=back

=back

=head1 FILTERS

These filters are used to modify input our output between a child
process and a scalar or subroutine endpoint.

=over

=item binary

   run \@cmd, ">", binary, \$out;
   run \@cmd, ">", binary, \$out;  ## Any TRUE value to enable
   run \@cmd, ">", binary 0, \$out;  ## Any FALSE value to disable

This is a constructor for a "binmode" "filter" that tells IPC::Run to keep
the carriage returns that would ordinarily be edited out for you (binmode
is usually off).  This is not a real filter, but an option masquerading as
a filter.

It's not named "binmode" because you're likely to want to call Perl's binmode
in programs that are piping binary data around.

=cut

sub binary(;$) {
    my $enable = @_ ? shift : 1;
    return bless sub { $enable }, "IPC::Run::binmode_pseudo_filter";
}

=pod

=item new_chunker

This breaks a stream of data in to chunks, based on an optional
scalar or regular expression parameter.  The default is the Perl
input record separator in $/, which is a newline be default.

   run \@cmd, '>', new_chunker, \&lines_handler;
   run \@cmd, '>', new_chunker( "\r\n" ), \&lines_handler;

Because this uses $/ by default, you should always pass in a parameter
if you are worried about other code (modules, etc) modifying $/.

If this filter is last in a filter chain that dumps in to a scalar,
the scalar must be set to '' before a new chunk will be written to it.

As an example of how a filter like this can be written, here's a
chunker that splits on newlines:

   sub line_splitter {
      my ( $in_ref, $out_ref ) = @_;

      return 0 if length $$out_ref;

      return input_avail && do {
         while (1) {
            if ( $$in_ref =~ s/\A(.*?\n)// ) {
               $$out_ref .= $1;
               return 1;
            }
            my $hmm = get_more_input;
            unless ( defined $hmm ) {
               $$out_ref = $$in_ref;
               $$in_ref = '';
               return length $$out_ref ? 1 : 0;
            }
            return 0 if $hmm eq 0;
         }
      }
   };

=cut

sub new_chunker(;$) {
    my ($re) = @_;
    $re = $/ if _empty $re;
    $re = quotemeta($re) unless ref $re eq 'Regexp';
    $re = qr/\A(.*?$re)/s;

    return sub {
        my ( $in_ref, $out_ref ) = @_;

        return 0 if length $$out_ref;

        return input_avail && do {
            while (1) {
                if ( $$in_ref =~ s/$re// ) {
                    $$out_ref .= $1;
                    return 1;
                }
                my $hmm = get_more_input;
                unless ( defined $hmm ) {
                    $$out_ref = $$in_ref;
                    $$in_ref  = '';
                    return length $$out_ref ? 1 : 0;
                }
                return 0 if $hmm eq 0;
            }
          }
    };
}

=pod

=item new_appender

This appends a fixed string to each chunk of data read from the source
scalar or sub.  This might be useful if you're writing commands to a
child process that always must end in a fixed string, like "\n":

   run( \@cmd,
      '<', new_appender( "\n" ), \&commands,
   );

Here's a typical filter sub that might be created by new_appender():

   sub newline_appender {
      my ( $in_ref, $out_ref ) = @_;

      return input_avail && do {
         $$out_ref = join( '', $$out_ref, $$in_ref, "\n" );
         $$in_ref = '';
         1;
      }
   };

=cut

sub new_appender($) {
    my ($suffix) = @_;
    croak "\$suffix undefined" unless defined $suffix;

    return sub {
        my ( $in_ref, $out_ref ) = @_;

        return input_avail && do {
            $$out_ref = join( '', $$out_ref, $$in_ref, $suffix );
            $$in_ref = '';
            1;
          }
    };
}

=item new_string_source

TODO: Needs confirmation. Was previously undocumented. in this module.

This is a filter which is exportable. Returns a sub which appends the data passed in to the output buffer and returns 1 if data was appended. 0 if it was an empty string and undef if no data was passed. 

NOTE: Any additional variables passed to new_string_source will be passed to the sub every time it's called and appended to the output. 

=cut

sub new_string_source {
    my $ref;
    if ( @_ > 1 ) {
        $ref = [@_],
    }
    else {
        $ref = shift;
    }

    return ref $ref eq 'SCALAR'
      ? sub {
        my ( $in_ref, $out_ref ) = @_;

        return defined $$ref
          ? do {
            $$out_ref .= $$ref;
            my $r = length $$ref ? 1 : 0;
            $$ref = undef;
            $r;
          }
          : undef;
      }
      : sub {
        my ( $in_ref, $out_ref ) = @_;

        return @$ref
          ? do {
            my $s = shift @$ref;
            $$out_ref .= $s;
            length $s ? 1 : 0;
          }
          : undef;
      }
}

=item new_string_sink

TODO: Needs confirmation. Was previously undocumented.

This is a filter which is exportable. Returns a sub which pops the data out of the input stream and pushes it onto the string.

=cut

sub new_string_sink {
    my ($string_ref) = @_;

    return sub {
        my ( $in_ref, $out_ref ) = @_;

        return input_avail && do {
            $$string_ref .= $$in_ref;
            $$in_ref = '';
            1;
          }
    };
}

#=item timeout
#
#This function defines a time interval, starting from when start() is
#called, or when timeout() is called.  If all processes have not finished
#by the end of the timeout period, then a "process timed out" exception
#is thrown.
#
#The time interval may be passed in seconds, or as an end time in
#"HH:MM:SS" format (any non-digit other than '.' may be used as
#spacing and punctuation).  This is probably best shown by example:
#
#   $h->timeout( $val );
#
#   $val                     Effect
#   ======================== =====================================
#   undef                    Timeout timer disabled
#   ''                       Almost immediate timeout
#   0                        Almost immediate timeout
#   0.000001                 timeout > 0.0000001 seconds
#   30                       timeout > 30 seconds
#   30.0000001               timeout > 30 seconds
#   10:30                    timeout > 10 minutes, 30 seconds
#
#Timeouts are currently evaluated with a 1 second resolution, though
#this may change in the future.  This means that setting
#timeout($h,1) will cause a pokey child to be aborted sometime after
#one second has elapsed and typically before two seconds have elapsed.
#
#This sub does not check whether or not the timeout has expired already.
#
#Returns the number of seconds set as the timeout (this does not change
#as time passes, unless you call timeout( val ) again).
#
#The timeout does not include the time needed to fork() or spawn()
#the child processes, though some setup time for the child processes can
#included.  It also does not include the length of time it takes for
#the children to exit after they've closed all their pipes to the
#parent process.
#
#=cut
#
#sub timeout {
#   my IPC::Run $self = shift;
#
#   if ( @_ ) {
#      ( $self->{TIMEOUT} ) = @_;
#      $self->{TIMEOUT_END} = undef;
#      if ( defined $self->{TIMEOUT} ) {
#	 if ( $self->{TIMEOUT} =~ /[^\d.]/ ) {
#	    my @f = split( /[^\d\.]+/i, $self->{TIMEOUT} );
#	    unshift @f, 0 while @f < 3;
#	    $self->{TIMEOUT} = (($f[0]*60)+$f[1])*60+$f[2];
#	 }
#	 elsif ( $self->{TIMEOUT} =~ /^(\d*)(?:\.(\d*))/ ) {
#	    $self->{TIMEOUT} = $1 + 1;
#	 }
#	 $self->_calc_timeout_end if $self->{STATE} >= _started;
#      }
#   }
#   return $self->{TIMEOUT};
#}
#
#
#sub _calc_timeout_end {
#   my IPC::Run $self = shift;
#
#   $self->{TIMEOUT_END} = defined $self->{TIMEOUT}
#      ? time + $self->{TIMEOUT}
#      : undef;
#
#   ## We add a second because we might be at the very end of the current
#   ## second, and we want to guarantee that we don't have a timeout even
#   ## one second less then the timeout period.
#   ++$self->{TIMEOUT_END} if $self->{TIMEOUT};
#}

=pod

=item io

Takes a filename or filehandle, a redirection operator, optional filters,
and a source or destination (depends on the redirection operator).  Returns
an IPC::Run::IO object suitable for harness()ing (including via start()
or run()).

This is shorthand for 


   require IPC::Run::IO;

      ... IPC::Run::IO->new(...) ...

=cut

sub io {
    require IPC::Run::IO;
    IPC::Run::IO->new(@_);
}

=pod

=item timer

   $h = start( \@cmd, \$in, \$out, $t = timer( 5 ) );

   pump $h until $out =~ /expected stuff/ || $t->is_expired;

Instantiates a non-fatal timer.  pump() returns once each time a timer
expires.  Has no direct effect on run(), but you can pass a subroutine
to fire when the timer expires. 

See L</timeout> for building timers that throw exceptions on
expiration.

See L<IPC::Run::Timer/timer> for details.

=cut

# Doing the prototype suppresses 'only used once' on older perls.
sub timer;
*timer = \&IPC::Run::Timer::timer;

=pod

=item timeout

   $h = start( \@cmd, \$in, \$out, $t = timeout( 5 ) );

   pump $h until $out =~ /expected stuff/;

Instantiates a timer that throws an exception when it expires.
If you don't provide an exception, a default exception that matches
/^IPC::Run: .*timed out/ is thrown by default.  You can pass in your own
exception scalar or reference:

   $h = start(
      \@cmd, \$in, \$out,
      $t = timeout( 5, exception => 'slowpoke' ),
   );

or set the name used in debugging message and in the default exception
string:

   $h = start(
      \@cmd, \$in, \$out,
      timeout( 50, name => 'process timer' ),
      $stall_timer = timeout( 5, name => 'stall timer' ),
   );

   pump $h until $out =~ /started/;

   $in = 'command 1';
   $stall_timer->start;
   pump $h until $out =~ /command 1 finished/;

   $in = 'command 2';
   $stall_timer->start;
   pump $h until $out =~ /command 2 finished/;

   $in = 'very slow command 3';
   $stall_timer->start( 10 );
   pump $h until $out =~ /command 3 finished/;

   $stall_timer->start( 5 );
   $in = 'command 4';
   pump $h until $out =~ /command 4 finished/;

   $stall_timer->reset; # Prevent restarting or expirng
   finish $h;

See L</timer> for building non-fatal timers.

See L<IPC::Run::Timer/timer> for details.

=cut

# Doing the prototype suppresses 'only used once' on older perls.
sub timeout;
*timeout = \&IPC::Run::Timer::timeout;

=pod

=back

=head1 FILTER IMPLEMENTATION FUNCTIONS

These functions are for use from within filters.

=over

=item input_avail

Returns TRUE if input is available.  If none is available, then 
&get_more_input is called and its result is returned.

This is usually used in preference to &get_more_input so that the
calling filter removes all data from the $in_ref before more data
gets read in to $in_ref.

C<input_avail> is usually used as part of a return expression:

   return input_avail && do {
      ## process the input just gotten
      1;
   };

This technique allows input_avail to return the undef or 0 that a
filter normally returns when there's no input to process.  If a filter
stores intermediate values, however, it will need to react to an
undef:

   my $got = input_avail;
   if ( ! defined $got ) {
      ## No more input ever, flush internal buffers to $out_ref
   }
   return $got unless $got;
   ## Got some input, move as much as need be
   return 1 if $added_to_out_ref;

=cut

sub input_avail() {
    confess "Undefined FBUF ref for $filter_num+1"
      unless defined $filter_op->{FBUFS}->[ $filter_num + 1 ];
    length ${ $filter_op->{FBUFS}->[ $filter_num + 1 ] } || get_more_input;
}

=pod

=item get_more_input

This is used to fetch more input in to the input variable.  It returns
undef if there will never be any more input, 0 if there is none now,
but there might be in the future, and TRUE if more input was gotten.

C<get_more_input> is usually used as part of a return expression,
see L</input_avail> for more information.

=cut

##
## Filter implementation interface
##
sub get_more_input() {
    ++$filter_num;
    my $r = eval {
        confess "get_more_input() called and no more filters in chain"
          unless defined $filter_op->{FILTERS}->[$filter_num];
        $filter_op->{FILTERS}->[$filter_num]->(
            $filter_op->{FBUFS}->[ $filter_num + 1 ],
            $filter_op->{FBUFS}->[$filter_num],
        );    # if defined ${$filter_op->{FBUFS}->[$filter_num+1]};
    };
    --$filter_num;
    die $@ if $@;
    return $r;
}

1;

=pod

=back

=head1 TODO

These will be addressed as needed and as time allows.

Stall timeout.

Expose a list of child process objects.  When I do this,
each child process is likely to be blessed into IPC::Run::Proc.

$kid->abort(), $kid->kill(), $kid->signal( $num_or_name ).

Write tests for /(full_)?results?/ subs.

Currently, pump() and run() only work on systems where select() works on the
filehandles returned by pipe().  This does *not* include ActiveState on Win32,
although it does work on cygwin under Win32 (thought the tests whine a bit).
I'd like to rectify that, suggestions and patches welcome.

Likewise start() only fully works on fork()/exec() machines (well, just
fork() if you only ever pass perl subs as subprocesses).  There's
some scaffolding for calling Open3::spawn_with_handles(), but that's
untested, and not that useful with limited select().

Support for C<\@sub_cmd> as an argument to a command which
gets replaced with /dev/fd or the name of a temporary file containing foo's
output.  This is like <(sub_cmd ...) found in bash and csh (IIRC).

Allow multiple harnesses to be combined as independent sets of processes
in to one 'meta-harness'.

Allow a harness to be passed in place of an \@cmd.  This would allow
multiple harnesses to be aggregated.

Ability to add external file descriptors w/ filter chains and endpoints.

Ability to add timeouts and timing generators (i.e. repeating timeouts).

High resolution timeouts.

=head1 Win32 LIMITATIONS

=over

=item Fails on Win9X

If you want Win9X support, you'll have to debug it or fund me because I
don't use that system any more.  The Win32 subsysem has been extended to
use temporary files in simple run() invocations and these may actually
work on Win9X too, but I don't have time to work on it.

=item May deadlock on Win2K (but not WinNT4 or WinXPPro)

Spawning more than one subprocess on Win2K causes a deadlock I haven't
figured out yet, but simple uses of run() often work.  Passes all tests
on WinXPPro and WinNT.

=item no support yet for <pty< and >pty>

These are likely to be implemented as "<" and ">" with binmode on, not
sure.

=item no support for file descriptors higher than 2 (stderr)

Win32 only allows passing explicit fds 0, 1, and 2.  If you really, really need to pass file handles, us Win32API:: GetOsFHandle() or ::FdGetOsFHandle() to
get the integer handle and pass it to the child process using the command
line, environment, stdin, intermediary file, or other IPC mechanism.  Then
use that handle in the child (Win32API.pm provides ways to reconstitute
Perl file handles from Win32 file handles).

=item no support for subroutine subprocesses (CODE refs)

Can't fork(), so the subroutines would have no context, and closures certainly
have no meaning

Perhaps with Win32 fork() emulation, this can be supported in a limited
fashion, but there are other very serious problems with that: all parent
fds get dup()ed in to the thread emulating the forked process, and that
keeps the parent from being able to close all of the appropriate fds.

=item no support for init => sub {} routines.

Win32 processes are created from scratch, there is no way to do an init
routine that will affect the running child.  Some limited support might
be implemented one day, do chdir() and %ENV changes can be made.

=item signals

Win32 does not fully support signals.  signal() is likely to cause errors
unless sending a signal that Perl emulates, and C<kill_kill()> is immediately
fatal (there is no grace period).

=item helper processes

IPC::Run uses helper processes, one per redirected file, to adapt between the
anonymous pipe connected to the child and the TCP socket connected to the
parent.  This is a waste of resources and will change in the future to either
use threads (instead of helper processes) or a WaitForMultipleObjects call
(instead of select).  Please contact me if you can help with the
WaitForMultipleObjects() approach; I haven't figured out how to get at it
without C code.

=item shutdown pause

There seems to be a pause of up to 1 second between when a child program exits
and the corresponding sockets indicate that they are closed in the parent.
Not sure why.

=item binmode

binmode is not supported yet.  The underpinnings are implemented, just ask
if you need it.

=item IPC::Run::IO

IPC::Run::IO objects can be used on Unix to read or write arbitrary files.  On
Win32, they will need to use the same helper processes to adapt from
non-select()able filehandles to select()able ones (or perhaps
WaitForMultipleObjects() will work with them, not sure).

=item startup race conditions

There seems to be an occasional race condition between child process startup
and pipe closings.  It seems like if the child is not fully created by the time
CreateProcess returns and we close the TCP socket being handed to it, the
parent socket can also get closed.  This is seen with the Win32 pumper
applications, not the "real" child process being spawned.

I assume this is because the kernel hasn't gotten around to incrementing the
reference count on the child's end (since the child was slow in starting), so
the parent's closing of the child end causes the socket to be closed, thus
closing the parent socket.

Being a race condition, it's hard to reproduce, but I encountered it while
testing this code on a drive share to a samba box.  In this case, it takes
t/run.t a long time to spawn it's child processes (the parent hangs in the
first select for several seconds until the child emits any debugging output).

I have not seen it on local drives, and can't reproduce it at will,
unfortunately.  The symptom is a "bad file descriptor in select()" error, and,
by turning on debugging, it's possible to see that select() is being called on
a no longer open file descriptor that was returned from the _socket() routine
in Win32Helper.  There's a new confess() that checks for this ("PARENT_HANDLE
no longer open"), but I haven't been able to reproduce it (typically).

=back

=head1 LIMITATIONS

On Unix, requires a system that supports C<waitpid( $pid, WNOHANG )> so
it can tell if a child process is still running.

PTYs don't seem to be non-blocking on some versions of Solaris. Here's a
test script contributed by Borislav Deianov <borislav@ensim.com> to see
if you have the problem.  If it dies, you have the problem.

   #!/usr/bin/perl

   use IPC::Run qw(run);
   use Fcntl;
   use IO::Pty;

   sub makecmd {
       return ['perl', '-e', 
               '<STDIN>, print "\n" x '.$_[0].'; while(<STDIN>){last if /end/}'];
   }

   #pipe R, W;
   #fcntl(W, F_SETFL, O_NONBLOCK);
   #while (syswrite(W, "\n", 1)) { $pipebuf++ };
   #print "pipe buffer size is $pipebuf\n";
   my $pipebuf=4096;
   my $in = "\n" x ($pipebuf * 2) . "end\n";
   my $out;

   $SIG{ALRM} = sub { die "Never completed!\n" };

   print "reading from scalar via pipe...";
   alarm( 2 );
   run(makecmd($pipebuf * 2), '<', \$in, '>', \$out);
   alarm( 0 );
   print "done\n";

   print "reading from code via pipe... ";
   alarm( 2 );
   run(makecmd($pipebuf * 3), '<', sub { $t = $in; undef $in; $t}, '>', \$out);
   alarm( 0 );
   print "done\n";

   $pty = IO::Pty->new();
   $pty->blocking(0);
   $slave = $pty->slave();
   while ($pty->syswrite("\n", 1)) { $ptybuf++ };
   print "pty buffer size is $ptybuf\n";
   $in = "\n" x ($ptybuf * 3) . "end\n";

   print "reading via pty... ";
   alarm( 2 );
   run(makecmd($ptybuf * 3), '<pty<', \$in, '>', \$out);
   alarm(0);
   print "done\n";

No support for ';', '&&', '||', '{ ... }', etc: use perl's, since run()
returns TRUE when the command exits with a 0 result code.

Does not provide shell-like string interpolation.

No support for C<cd>, C<setenv>, or C<export>: do these in an init() sub

   run(
      \cmd,
         ...
         init => sub {
            chdir $dir or die $!;
            $ENV{FOO}='BAR'
         }
   );

Timeout calculation does not allow absolute times, or specification of
days, months, etc.

B<WARNING:> Function coprocesses (C<run \&foo, ...>) suffer from two
limitations.  The first is that it is difficult to close all filehandles the
child inherits from the parent, since there is no way to scan all open
FILEHANDLEs in Perl and it both painful and a bit dangerous to close all open
file descriptors with C<POSIX::close()>. Painful because we can't tell which
fds are open at the POSIX level, either, so we'd have to scan all possible fds
and close any that we don't want open (normally C<exec()> closes any
non-inheritable but we don't C<exec()> for &sub processes.

The second problem is that Perl's DESTROY subs and other on-exit cleanup gets
run in the child process.  If objects are instantiated in the parent before the
child is forked, the DESTROY will get run once in the parent and once in
the child.  When coprocess subs exit, POSIX::exit is called to work around this,
but it means that objects that are still referred to at that time are not
cleaned up.  So setting package vars or closure vars to point to objects that
rely on DESTROY to affect things outside the process (files, etc), will
lead to bugs.

I goofed on the syntax: "<pipe" vs. "<pty<" and ">filename" are both
oddities.

=head1 TODO

=over

=item Allow one harness to "adopt" another:

   $new_h = harness \@cmd2;
   $h->adopt( $new_h );

=item Close all filehandles not explicitly marked to stay open.

The problem with this one is that there's no good way to scan all open
FILEHANDLEs in Perl, yet you don't want child processes inheriting handles
willy-nilly.

=back

=head1 INSPIRATION

Well, select() and waitpid() badly needed wrapping, and open3() isn't
open-minded enough for me.

The shell-like API inspired by a message Russ Allbery sent to perl5-porters,
which included:

   I've thought for some time that it would be
   nice to have a module that could handle full Bourne shell pipe syntax
   internally, with fork and exec, without ever invoking a shell.  Something
   that you could give things like:

   pipeopen (PIPE, [ qw/cat file/ ], '|', [ 'analyze', @args ], '>&3');

Message ylln51p2b6.fsf@windlord.stanford.edu, on 2000/02/04.

=head1 SUPPORT

Bugs should always be submitted via the CPAN bug tracker

L<http://rt.cpan.org/NoAuth/ReportBug.html?Queue=IPC-Run>

For other issues, contact the maintainer (the first listed author)

=head1 AUTHORS

Adam Kennedy <adamk@cpan.org>

Barrie Slaymaker <barries@slaysys.com>

=head1 COPYRIGHT

Some parts copyright 2008 - 2009 Adam Kennedy.

Copyright 1999 Barrie Slaymaker.

You may distribute under the terms of either the GNU General Public
License or the Artistic License, as specified in the README file.

=cut
