#!/usr/bin/perl
#
# Scrub the variable fields from the perf trace2 output to
# make testing easier.

use strict;
use warnings;

my $qpath = '\'[^\']*\'|[^ ]*';

my $col_depth=0;
my $col_thread=1;
my $col_event=2;
my $col_repo=3;
my $col_t_abs=4;
my $col_t_rel=5;
my $col_category=6;
my $col_rest=7;

# This code assumes that the trace2 data was written with bare
# turned on (which omits the "<clock> <file>:<line> | <parents>"
# prefix.

while (<>) {
    my @tokens = split /\|/;

    foreach my $col (@tokens) { $col =~ s/^\s+|\s+$//g; }

    if ($tokens[$col_event] =~ m/^start/) {
	# The 'start' message lists the contents of argv in $col_rest.
	# On some platforms (Windows), argv[0] is *sometimes* a canonical
	# absolute path to the EXE rather than the value passed in the
	# shell script.  Replace it with a placeholder to simplify our
	# HEREDOC in the test script.
	my $argv0;
	my $argvRest;
	$tokens[$col_rest] =~ s/^($qpath)\W*(.*)/_EXE_ $2/;
    }
    elsif ($tokens[$col_event] =~ m/cmd_path/) {
	# Likewise, the 'cmd_path' message breaks out argv[0].
	#
	# This line is only emitted when RUNTIME_PREFIX is defined,
	# so just omit it for testing purposes.
	# $tokens[$col_rest] = "_EXE_";
	goto SKIP_LINE;
    }
    elsif ($tokens[$col_event] =~ m/cmd_ancestry/) {
	# 'cmd_ancestry' is platform-specific and not implemented everywhere,
	# so skip it.
	goto SKIP_LINE;
    }
    elsif ($tokens[$col_event] =~ m/child_exit/) {
	$tokens[$col_rest] =~ s/ pid:\d* / pid:_PID_ /;
    }
    elsif ($tokens[$col_event] =~ m/data/) {
	if ($tokens[$col_category] =~ m/process/) {
	    # 'data' and 'data_json' events containing 'process'
	    # category data are assumed to be platform-specific
	    # and highly variable.  Just omit them.
	    goto SKIP_LINE;
	}
    }

    # t_abs and t_rel are either blank or a float.  Replace the float
    # with a constant for matching the HEREDOC in the test script.
    if ($tokens[$col_t_abs] =~ m/\d/) {
	$tokens[$col_t_abs] = "_T_ABS_";
    }
    if ($tokens[$col_t_rel] =~ m/\d/) {
	$tokens[$col_t_rel] = "_T_REL_";
    }

    my $out;

    $out = join('|', @tokens);
    print "$out\n";

  SKIP_LINE:
}


