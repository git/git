#!/usr/bin/perl
#
# Parse event stream and convert individual events into a summary
# record for the process.
#
# Git.exe generates one or more "event" records for each API method,
# such as "start <argv>" and "exit <code>", during the life of the git
# process.  Additionally, the input may contain interleaved events
# from multiple concurrent git processes and/or multiple threads from
# within a git process.
#
# Accumulate events for each process (based on its unique SID) in a
# dictionary and emit process summary records.
#
# Convert some of the variable fields (such as elapsed time) into
# placeholders (or omit them) to make HEREDOC comparisons easier in
# the test scripts.
#
# We may also omit fields not (currently) useful for testing purposes.

use strict;
use warnings;
use JSON::PP;
use Data::Dumper;
use Getopt::Long;

# The version of the trace2 event target format that we understand.
# This is reported in the 'version' event in the 'evt' field.
# It comes from the GIT_TRACE2_EVENT_VERSION macro in trace2/tr2_tgt_event.c
my $evt_version = '1';

my $show_children = 1;
my $show_exec     = 1;
my $show_threads  = 1;

# A hack to generate test HEREDOC data for pasting into the test script.
# Usage:
#    cd "t/trash directory.t0212-trace2-event"
#    $TT trace ... >trace.event
#    VV=$(../../git.exe version | sed -e 's/^git version //')
#    perl ../t0212/parse_events.perl --HEREDOC --VERSION=$VV <trace.event >heredoc
# Then paste heredoc into your new test.

my $gen_heredoc = 0;
my $gen_version = '';

GetOptions("children!" => \$show_children,
	   "exec!"     => \$show_exec,
	   "threads!"  => \$show_threads,
	   "HEREDOC!"  => \$gen_heredoc,
	   "VERSION=s" => \$gen_version    )
    or die("Error in command line arguments\n");


# SIDs contains timestamps and PIDs of the process and its parents.
# This makes it difficult to match up in a HEREDOC in the test script.
# Build a map from actual SIDs to predictable constant values and yet
# keep the parent/child relationships.  For example:
# {..., "sid":"1539706952458276-8652", ...}
# {..., "sid":"1539706952458276-8652/1539706952649493-15452", ...}
# becomes:
# {..., "sid":"_SID1_", ...}
# {..., "sid":"_SID1_/_SID2_", ...}
my $sid_map;
my $sid_count = 0;

my $processes;

while (<>) {
    my $line = decode_json( $_ );

    my $sid = "";
    my $sid_sep = "";

    my $raw_sid = $line->{'sid'};
    my @raw_sid_parts = split /\//, $raw_sid;
    foreach my $raw_sid_k (@raw_sid_parts) {
	if (!exists $sid_map->{$raw_sid_k}) {
	    $sid_map->{$raw_sid_k} = '_SID' . $sid_count . '_';
	    $sid_count++;
	}
	$sid = $sid . $sid_sep . $sid_map->{$raw_sid_k};
	$sid_sep = '/';
    }
    
    my $event = $line->{'event'};

    if ($event eq 'version') {
	$processes->{$sid}->{'version'} = $line->{'exe'};
	if ($gen_heredoc == 1 && $gen_version eq $line->{'exe'}) {
	    # If we are generating data FOR the test script, replace
	    # the reported git.exe version with a reference to an
	    # environment variable.  When our output is pasted into
	    # the test script, it will then be expanded in future
	    # test runs to the THEN current version of git.exe.
	    # We assume that the test script uses env var $V.
	    $processes->{$sid}->{'version'} = "\$V";
	}
    }

    elsif ($event eq 'start') {
	$processes->{$sid}->{'argv'} = $line->{'argv'};
	$processes->{$sid}->{'argv'}[0] = "_EXE_";
    }

    elsif ($event eq 'exit') {
	$processes->{$sid}->{'exit_code'} = $line->{'code'};
    }

    elsif ($event eq 'atexit') {
	$processes->{$sid}->{'exit_code'} = $line->{'code'};
    }

    elsif ($event eq 'error') {
	# For HEREDOC purposes, use the error message format string if
	# available, rather than the formatted message (which probably
	# has an absolute pathname).
	if (exists $line->{'fmt'}) {
	    push( @{$processes->{$sid}->{'errors'}}, $line->{'fmt'} );
	}
	elsif (exists $line->{'msg'}) {
	    push( @{$processes->{$sid}->{'errors'}}, $line->{'msg'} );
	}
    }

    elsif ($event eq 'cmd_path') {
	## $processes->{$sid}->{'path'} = $line->{'path'};
	#
	# Like in the 'start' event, we need to replace the value of
	# argv[0] with a token for HEREDOC purposes.  However, the
	# event is only emitted when RUNTIME_PREFIX is defined, so
	# just omit it for testing purposes.
	# $processes->{$sid}->{'path'} = "_EXE_";
    }
    elsif ($event eq 'cmd_ancestry') {
	# 'cmd_ancestry' is platform-specific and not implemented everywhere, so
	# just skip it for testing purposes.
    }
    elsif ($event eq 'cmd_name') {
	$processes->{$sid}->{'name'} = $line->{'name'};
	$processes->{$sid}->{'hierarchy'} = $line->{'hierarchy'};
    }

    elsif ($event eq 'alias') {
	$processes->{$sid}->{'alias'}->{'key'} = $line->{'alias'};
	$processes->{$sid}->{'alias'}->{'argv'} = $line->{'argv'};
    }

    elsif ($event eq 'def_param') {
	my $kv;
	$kv->{'param'} = $line->{'param'};
	$kv->{'value'} = $line->{'value'};
	push( @{$processes->{$sid}->{'params'}}, $kv );
    }

    elsif ($event eq 'child_start') {
	if ($show_children == 1) {
	    $processes->{$sid}->{'child'}->{$line->{'child_id'}}->{'child_class'} = $line->{'child_class'};
	    $processes->{$sid}->{'child'}->{$line->{'child_id'}}->{'child_argv'} = $line->{'argv'};
	    $processes->{$sid}->{'child'}->{$line->{'child_id'}}->{'child_argv'}[0] = "_EXE_";
	    $processes->{$sid}->{'child'}->{$line->{'child_id'}}->{'use_shell'} = $line->{'use_shell'} ? 1 : 0;
	}
    }

    elsif ($event eq 'child_exit') {
	if ($show_children == 1) {
	    $processes->{$sid}->{'child'}->{$line->{'child_id'}}->{'child_code'} = $line->{'code'};
	}
    }

    # TODO decide what information we want to test from thread events.

    elsif ($event eq 'thread_start') {
	if ($show_threads == 1) {
	}
    }

    elsif ($event eq 'thread_exit') {
	if ($show_threads == 1) {
	}
    }

    # TODO decide what information we want to test from exec events.

    elsif ($event eq 'exec') {
	if ($show_exec == 1) {
	}
    }

    elsif ($event eq 'exec_result') {
	if ($show_exec == 1) {
	}
    }

    elsif ($event eq 'def_param') {
	# Accumulate parameter key/value pairs by key rather than in an array
	# so that we get overwrite (last one wins) effects.
	$processes->{$sid}->{'params'}->{$line->{'param'}} = $line->{'value'};
    }

    elsif ($event eq 'def_repo') {
	# $processes->{$sid}->{'repos'}->{$line->{'repo'}} = $line->{'worktree'};
	$processes->{$sid}->{'repos'}->{$line->{'repo'}} = "_WORKTREE_";
    }

    # A series of potentially nested and threaded region and data events
    # is fundamentally incompatibile with the type of summary record we
    # are building in this script.  Since they are intended for
    # perf-trace-like analysis rather than a result summary, we ignore
    # most of them here.

    # elsif ($event eq 'region_enter') {
    # }
    # elsif ($event eq 'region_leave') {
    # }

    elsif ($event eq 'data') {
	my $cat = $line->{'category'};
	my $key = $line->{'key'};
	my $value = $line->{'value'};
	$processes->{$sid}->{'data'}->{$cat}->{$key} = $value;
    }

    elsif ($event eq 'data_json') {
	# NEEDSWORK: Ignore due to
	# compat/win32/trace2_win32_process_info.c, which should log a
	# "cmd_ancestry" event instead.
    }

    else {
	push @{$processes->{$sid}->{$event}} => $line->{value};
    }

    # This trace2 target does not emit 'printf' events.
    #
    # elsif ($event eq 'printf') {
    # }
}

# Dump the resulting hash into something that we can compare against
# in the test script.  These options make Dumper output look a little
# bit like JSON.  Also convert variable references of the form "$VAR*"
# so that the matching HEREDOC doesn't need to escape it.

$Data::Dumper::Sortkeys = 1;
$Data::Dumper::Indent = 1;
$Data::Dumper::Purity = 1;
$Data::Dumper::Pair = ':';

my $out = Dumper($processes);
$out =~ s/'/"/g;
$out =~ s/\$VAR/VAR/g;

# Finally, if we're running this script to generate (manually confirmed)
# data to add to the test script, guard the indentation.

if ($gen_heredoc == 1) {
    $out =~ s/^/\t\|/gms;
}

print $out;
