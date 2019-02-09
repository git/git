#!/usr/bin/perl
#
# Scrub the variable fields from the normal trace2 output to
# make testing easier.

use strict;
use warnings;

my $float = '[0-9]*\.[0-9]+([eE][-+]?[0-9]+)?';

# This code assumes that the trace2 data was written with bare
# turned on (which omits the "<clock> <file>:<line>" prefix.

while (<>) {
    # Various messages include an elapsed time in the middle
    # of the message.  Replace the time with a placeholder to
    # simplify our HEREDOC in the test script.
    s/elapsed:$float/elapsed:_TIME_/g;

    my $line = $_;

    # we expect:
    #    start <argv0> [<argv1> [<argv2> [...]]]
    #
    # where argv0 might be a relative or absolute path, with
    # or without quotes, and platform dependent.  Replace argv0
    # with a token for HEREDOC matching in the test script.

    if ($line =~ m/^start/) {
	$line =~ /^start\s+(.*)/;
	my $argv = $1;
	$argv =~ m/(\'[^\']*\'|[^ ]+)\s+(.*)/;
	my $argv_0 = $1;
	my $argv_rest = $2;

	print "start _EXE_ $argv_rest\n";
    }
    elsif ($line =~ m/^cmd_path/) {
	# Likewise, the 'cmd_path' message breaks out argv[0].
	#
	# This line is only emitted when RUNTIME_PREFIX is defined,
	# so just omit it for testing purposes.
	# print "cmd_path _EXE_\n";
    }
    else {
	print "$line";
    }
}
