#!/bin/sh

test_description='test trace2 facility (perf target)'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Turn off any inherited trace2 settings for this test.
sane_unset GIT_TRACE2 GIT_TRACE2_PERF GIT_TRACE2_EVENT
sane_unset GIT_TRACE2_PERF_BRIEF
sane_unset GIT_TRACE2_CONFIG_PARAMS

# Add t/helper directory to PATH so that we can use a relative
# path to run nested instances of test-tool.exe (see 004child).
# This helps with HEREDOC comparisons later.
TTDIR="$GIT_BUILD_DIR/t/helper/" && export TTDIR
PATH="$TTDIR:$PATH" && export PATH

# Warning: use of 'test_cmp' may run test-tool.exe and/or git.exe
# Warning: to do the actual diff/comparison, so the HEREDOCs here
# Warning: only cover our actual calls to test-tool and/or git.
# Warning: So you may see extra lines in artifact files when
# Warning: interactively debugging.

V=$(git version | sed -e 's/^git version //') && export V

# There are multiple trace2 targets: normal, perf, and event.
# Trace2 events will/can be written to each active target (subject
# to whatever filtering that target decides to do).
# Test each target independently.
#
# Defer setting GIT_TRACE2_PERF until the actual command we want to
# test because hidden git and test-tool commands in the test
# harness can contaminate our output.

# Enable "brief" feature which turns off the prefix:
#     "<clock> <file>:<line> | <nr_parents> | "
GIT_TRACE2_PERF_BRIEF=1 && export GIT_TRACE2_PERF_BRIEF

# Repeat some of the t0210 tests using the perf target stream instead of
# the normal stream.
#
# Tokens here of the form _FIELD_ have been replaced in the observed output.

# Verb 001return
#
# Implicit return from cmd_<verb> function propagates <code>.

test_expect_success 'perf stream, return code 0' '
	test_when_finished "rm trace.perf actual expect" &&
	GIT_TRACE2_PERF="$(pwd)/trace.perf" test-tool trace2 001return 0 &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&
	cat >expect <<-EOF &&
		d0|main|version|||||$V
		d0|main|start||_T_ABS_|||_EXE_ trace2 001return 0
		d0|main|cmd_name|||||trace2 (trace2)
		d0|main|exit||_T_ABS_|||code:0
		d0|main|atexit||_T_ABS_|||code:0
	EOF
	test_cmp expect actual
'

test_expect_success 'perf stream, return code 1' '
	test_when_finished "rm trace.perf actual expect" &&
	test_must_fail env GIT_TRACE2_PERF="$(pwd)/trace.perf" test-tool trace2 001return 1 &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&
	cat >expect <<-EOF &&
		d0|main|version|||||$V
		d0|main|start||_T_ABS_|||_EXE_ trace2 001return 1
		d0|main|cmd_name|||||trace2 (trace2)
		d0|main|exit||_T_ABS_|||code:1
		d0|main|atexit||_T_ABS_|||code:1
	EOF
	test_cmp expect actual
'

# Verb 003error
#
# To the above, add multiple 'error <msg>' events

test_expect_success 'perf stream, error event' '
	test_when_finished "rm trace.perf actual expect" &&
	GIT_TRACE2_PERF="$(pwd)/trace.perf" test-tool trace2 003error "hello world" "this is a test" &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&
	cat >expect <<-EOF &&
		d0|main|version|||||$V
		d0|main|start||_T_ABS_|||_EXE_ trace2 003error '\''hello world'\'' '\''this is a test'\''
		d0|main|cmd_name|||||trace2 (trace2)
		d0|main|error|||||hello world
		d0|main|error|||||this is a test
		d0|main|exit||_T_ABS_|||code:0
		d0|main|atexit||_T_ABS_|||code:0
	EOF
	test_cmp expect actual
'

# Verb 004child
#
# Test nested spawning of child processes.
#
# Conceptually, this looks like:
#    P1: TT trace2 004child
#    P2: |--- TT trace2 004child
#    P3:      |--- TT trace2 001return 0
#
# Which should generate events:
#    P1: version
#    P1: start
#    P1: cmd_name
#    P1: child_start
#        P2: version
#        P2: start
#        P2: cmd_name
#        P2: child_start
#            P3: version
#            P3: start
#            P3: cmd_name
#            P3: exit
#            P3: atexit
#        P2: child_exit
#        P2: exit
#        P2: atexit
#    P1: child_exit
#    P1: exit
#    P1: atexit

test_expect_success 'perf stream, child processes' '
	test_when_finished "rm trace.perf actual expect" &&
	GIT_TRACE2_PERF="$(pwd)/trace.perf" test-tool trace2 004child test-tool trace2 004child test-tool trace2 001return 0 &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&
	cat >expect <<-EOF &&
		d0|main|version|||||$V
		d0|main|start||_T_ABS_|||_EXE_ trace2 004child test-tool trace2 004child test-tool trace2 001return 0
		d0|main|cmd_name|||||trace2 (trace2)
		d0|main|child_start||_T_ABS_|||[ch0] class:? argv:[test-tool trace2 004child test-tool trace2 001return 0]
		d1|main|version|||||$V
		d1|main|start||_T_ABS_|||_EXE_ trace2 004child test-tool trace2 001return 0
		d1|main|cmd_name|||||trace2 (trace2/trace2)
		d1|main|child_start||_T_ABS_|||[ch0] class:? argv:[test-tool trace2 001return 0]
		d2|main|version|||||$V
		d2|main|start||_T_ABS_|||_EXE_ trace2 001return 0
		d2|main|cmd_name|||||trace2 (trace2/trace2/trace2)
		d2|main|exit||_T_ABS_|||code:0
		d2|main|atexit||_T_ABS_|||code:0
		d1|main|child_exit||_T_ABS_|_T_REL_||[ch0] pid:_PID_ code:0
		d1|main|exit||_T_ABS_|||code:0
		d1|main|atexit||_T_ABS_|||code:0
		d0|main|child_exit||_T_ABS_|_T_REL_||[ch0] pid:_PID_ code:0
		d0|main|exit||_T_ABS_|||code:0
		d0|main|atexit||_T_ABS_|||code:0
	EOF
	test_cmp expect actual
'

sane_unset GIT_TRACE2_PERF_BRIEF

# Now test without environment variables and get all Trace2 settings
# from the global config.

test_expect_success 'using global config, perf stream, return code 0' '
	test_when_finished "rm trace.perf actual expect" &&
	test_config_global trace2.perfBrief 1 &&
	test_config_global trace2.perfTarget "$(pwd)/trace.perf" &&
	test-tool trace2 001return 0 &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&
	cat >expect <<-EOF &&
		d0|main|version|||||$V
		d0|main|start||_T_ABS_|||_EXE_ trace2 001return 0
		d0|main|cmd_name|||||trace2 (trace2)
		d0|main|exit||_T_ABS_|||code:0
		d0|main|atexit||_T_ABS_|||code:0
	EOF
	test_cmp expect actual
'

# Exercise the stopwatch timers in a loop and confirm that we have
# as many start/stop intervals as expected.  We cannot really test the
# actual (total, min, max) timer values, so we have to assume that they
# are good, but we can verify the interval count.
#
# The timer "test/test1" should only emit a global summary "timer" event.
# The timer "test/test2" should emit per-thread "th_timer" events and a
# global summary "timer" event.

have_timer_event () {
	thread=$1 event=$2 category=$3 name=$4 intervals=$5 file=$6 &&

	pattern="d0|${thread}|${event}||||${category}|name:${name} intervals:${intervals}" &&

	grep "${pattern}" ${file}
}

test_expect_success 'stopwatch timer test/test1' '
	test_when_finished "rm trace.perf actual" &&
	test_config_global trace2.perfBrief 1 &&
	test_config_global trace2.perfTarget "$(pwd)/trace.perf" &&

	# Use the timer "test1" 5 times from "main".
	test-tool trace2 100timer 5 10 &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&

	have_timer_event "main" "timer" "test" "test1" 5 actual
'

test_expect_success PTHREAD 'stopwatch timer test/test2' '
	test_when_finished "rm trace.perf actual" &&
	test_config_global trace2.perfBrief 1 &&
	test_config_global trace2.perfTarget "$(pwd)/trace.perf" &&

	# Use the timer "test2" 5 times each in 3 threads.
	test-tool trace2 101timer 5 10 3 &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&

	# So we should have 3 per-thread events of 5 each.
	have_timer_event "th01:ut_101" "th_timer" "test" "test2" 5 actual &&
	have_timer_event "th02:ut_101" "th_timer" "test" "test2" 5 actual &&
	have_timer_event "th03:ut_101" "th_timer" "test" "test2" 5 actual &&

	# And we should have 15 total uses.
	have_timer_event "main" "timer" "test" "test2" 15 actual
'

# Exercise the global counters and confirm that we get the expected values.
#
# The counter "test/test1" should only emit a global summary "counter" event.
# The counter "test/test2" could emit per-thread "th_counter" events and a
# global summary "counter" event.

have_counter_event () {
	thread=$1 event=$2 category=$3 name=$4 value=$5 file=$6 &&

	pattern="d0|${thread}|${event}||||${category}|name:${name} value:${value}" &&

	grep "${patern}" ${file}
}

test_expect_success 'global counter test/test1' '
	test_when_finished "rm trace.perf actual" &&
	test_config_global trace2.perfBrief 1 &&
	test_config_global trace2.perfTarget "$(pwd)/trace.perf" &&

	# Use the counter "test1" and add n integers.
	test-tool trace2 200counter 1 2 3 4 5 &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&

	have_counter_event "main" "counter" "test" "test1" 15 actual
'

test_expect_success PTHREAD 'global counter test/test2' '
	test_when_finished "rm trace.perf actual" &&
	test_config_global trace2.perfBrief 1 &&
	test_config_global trace2.perfTarget "$(pwd)/trace.perf" &&

	# Add 2 integers to the counter "test2" in each of 3 threads.
	test-tool trace2 201counter 7 13 3 &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <trace.perf >actual &&

	# So we should have 3 per-thread events of 5 each.
	have_counter_event "th01:ut_201" "th_counter" "test" "test2" 20 actual &&
	have_counter_event "th02:ut_201" "th_counter" "test" "test2" 20 actual &&
	have_counter_event "th03:ut_201" "th_counter" "test" "test2" 20 actual &&

	# And we should have a single event with the total across all threads.
	have_counter_event "main" "counter" "test" "test2" 60 actual
'

test_done
