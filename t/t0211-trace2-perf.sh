#!/bin/sh

test_description='test trace2 facility (perf target)'

TEST_PASSES_SANITIZE_LEAK=false
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

test_expect_success PTHREADS 'stopwatch timer test/test2' '
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

	grep "${pattern}" ${file}
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

test_expect_success PTHREADS 'global counter test/test2' '
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

test_expect_success 'unsafe URLs are redacted by default' '
	test_when_finished \
		"rm -r actual trace.perf unredacted.perf clone clone2" &&

	test_config_global \
		"url.$(pwd).insteadOf" https://user:pwd@example.com/ &&
	test_config_global trace2.configParams "core.*,remote.*.url" &&

	GIT_TRACE2_PERF="$(pwd)/trace.perf" \
		git clone https://user:pwd@example.com/ clone &&
	! grep user:pwd trace.perf &&

	GIT_TRACE2_REDACT=0 GIT_TRACE2_PERF="$(pwd)/unredacted.perf" \
		git clone https://user:pwd@example.com/ clone2 &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <unredacted.perf >actual &&
	grep "d0|main|start|.* clone https://user:pwd@example.com" actual &&
	grep "d0|main|def_param|.*|remote.origin.url:https://user:pwd@example.com" actual
'

# Confirm that the requested command produces a "cmd_name" and a
# set of "def_param" events.
#
try_simple () {
	test_when_finished "rm prop.perf actual" &&

	cmd=$1 &&
	cmd_name=$2 &&

	test_config_global "trace2.configParams" "cfg.prop.*" &&
	test_config_global "trace2.envvars" "ENV_PROP_FOO,ENV_PROP_BAR" &&

	test_config_global "cfg.prop.foo" "red" &&

	ENV_PROP_FOO=blue \
		GIT_TRACE2_PERF="$(pwd)/prop.perf" \
			$cmd &&
	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <prop.perf >actual &&
	grep "d0|main|cmd_name|.*|$cmd_name" actual &&
	grep "d0|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d0|main|def_param|.*|ENV_PROP_FOO:blue" actual
}

# Representative mainstream builtin Git command dispatched
# in run_builtin() in git.c
#
test_expect_success 'expect def_params for normal builtin command' '
	try_simple "git version" "version"
'

# Representative query command dispatched in handle_options()
# in git.c
#
test_expect_success 'expect def_params for query command' '
	try_simple "git --man-path" "_query_"
'

# remote-curl.c does not use the builtin setup in git.c, so confirm
# that executables built from remote-curl.c emit def_params.
#
# Also tests the dashed-command handling where "git foo" silently
# spawns "git-foo".  Make sure that both commands should emit
# def_params.
#
# Pass bogus arguments to remote-https and allow the command to fail
# because we don't actually have a remote to fetch from.  We just want
# to see the run-dashed code run an executable built from
# remote-curl.c rather than git.c.  Confirm that we get def_param
# events from both layers.
#
test_expect_success 'expect def_params for remote-curl and _run_dashed_' '
	test_when_finished "rm prop.perf actual" &&

	test_config_global "trace2.configParams" "cfg.prop.*" &&
	test_config_global "trace2.envvars" "ENV_PROP_FOO,ENV_PROP_BAR" &&

	test_config_global "cfg.prop.foo" "red" &&

	test_might_fail env \
		ENV_PROP_FOO=blue \
		GIT_TRACE2_PERF="$(pwd)/prop.perf" \
		git remote-http x y &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <prop.perf >actual &&

	grep "d0|main|cmd_name|.*|_run_dashed_" actual &&
	grep "d0|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d0|main|def_param|.*|ENV_PROP_FOO:blue" actual &&

	grep "d1|main|cmd_name|.*|remote-curl" actual &&
	grep "d1|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d1|main|def_param|.*|ENV_PROP_FOO:blue" actual
'

# Similarly, `git-http-fetch` is not built from git.c so do a
# trivial fetch so that the main git.c run-dashed code spawns
# an executable built from http-fetch.c.  Confirm that we get
# def_param events from both layers.
#
test_expect_success 'expect def_params for http-fetch and _run_dashed_' '
	test_when_finished "rm prop.perf actual" &&

	test_config_global "trace2.configParams" "cfg.prop.*" &&
	test_config_global "trace2.envvars" "ENV_PROP_FOO,ENV_PROP_BAR" &&

	test_config_global "cfg.prop.foo" "red" &&

	test_might_fail env \
		ENV_PROP_FOO=blue \
		GIT_TRACE2_PERF="$(pwd)/prop.perf" \
		git http-fetch --stdin file:/// <<-EOF &&
	EOF

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <prop.perf >actual &&

	grep "d0|main|cmd_name|.*|_run_dashed_" actual &&
	grep "d0|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d0|main|def_param|.*|ENV_PROP_FOO:blue" actual &&

	grep "d1|main|cmd_name|.*|http-fetch" actual &&
	grep "d1|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d1|main|def_param|.*|ENV_PROP_FOO:blue" actual
'

# Historically, alias expansion explicitly emitted the def_param
# events (independent of whether the command was a builtin, a Git
# command or arbitrary shell command) so that it wasn't dependent
# upon the unpeeling of the alias. Let's make sure that we preserve
# the net effect.
#
test_expect_success 'expect def_params during git alias expansion' '
	test_when_finished "rm prop.perf actual" &&

	test_config_global "trace2.configParams" "cfg.prop.*" &&
	test_config_global "trace2.envvars" "ENV_PROP_FOO,ENV_PROP_BAR" &&

	test_config_global "cfg.prop.foo" "red" &&

	test_config_global "alias.xxx" "version" &&

	ENV_PROP_FOO=blue \
		GIT_TRACE2_PERF="$(pwd)/prop.perf" \
			git xxx &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <prop.perf >actual &&

	# "git xxx" is first mapped to "git-xxx" and the child will fail.
	grep "d0|main|cmd_name|.*|_run_dashed_ (_run_dashed_)" actual &&

	# We unpeel that and substitute "version" into "xxx" (giving
	# "git version") and update the cmd_name event.
	grep "d0|main|cmd_name|.*|_run_git_alias_ (_run_dashed_/_run_git_alias_)" actual &&

	# These def_param events could be associated with either of the
	# above cmd_name events.  It does not matter.
	grep "d0|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d0|main|def_param|.*|ENV_PROP_FOO:blue" actual &&

	# The "git version" child sees a different cmd_name hierarchy.
	# Also test the def_param (only for completeness).
	grep "d1|main|cmd_name|.*|version (_run_dashed_/_run_git_alias_/version)" actual &&
	grep "d1|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d1|main|def_param|.*|ENV_PROP_FOO:blue" actual
'

test_expect_success 'expect def_params during shell alias expansion' '
	test_when_finished "rm prop.perf actual" &&

	test_config_global "trace2.configParams" "cfg.prop.*" &&
	test_config_global "trace2.envvars" "ENV_PROP_FOO,ENV_PROP_BAR" &&

	test_config_global "cfg.prop.foo" "red" &&

	test_config_global "alias.xxx" "!git version" &&

	ENV_PROP_FOO=blue \
		GIT_TRACE2_PERF="$(pwd)/prop.perf" \
			git xxx &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <prop.perf >actual &&

	# "git xxx" is first mapped to "git-xxx" and the child will fail.
	grep "d0|main|cmd_name|.*|_run_dashed_ (_run_dashed_)" actual &&

	# We unpeel that and substitute "git version" for "git xxx" (as a
	# shell command.  Another cmd_name event is emitted as we unpeel.
	grep "d0|main|cmd_name|.*|_run_shell_alias_ (_run_dashed_/_run_shell_alias_)" actual &&

	# These def_param events could be associated with either of the
	# above cmd_name events.  It does not matter.
	grep "d0|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d0|main|def_param|.*|ENV_PROP_FOO:blue" actual &&

	# We get the following only because we used a git command for the
	# shell command. In general, it could have been a shell script and
	# we would see nothing.
	#
	# The child knows the cmd_name hierarchy so it includes it.
	grep "d1|main|cmd_name|.*|version (_run_dashed_/_run_shell_alias_/version)" actual &&
	grep "d1|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d1|main|def_param|.*|ENV_PROP_FOO:blue" actual
'

test_expect_success 'expect def_params during nested git alias expansion' '
	test_when_finished "rm prop.perf actual" &&

	test_config_global "trace2.configParams" "cfg.prop.*" &&
	test_config_global "trace2.envvars" "ENV_PROP_FOO,ENV_PROP_BAR" &&

	test_config_global "cfg.prop.foo" "red" &&

	test_config_global "alias.xxx" "yyy" &&
	test_config_global "alias.yyy" "version" &&

	ENV_PROP_FOO=blue \
		GIT_TRACE2_PERF="$(pwd)/prop.perf" \
			git xxx &&

	perl "$TEST_DIRECTORY/t0211/scrub_perf.perl" <prop.perf >actual &&

	# "git xxx" is first mapped to "git-xxx" and try to spawn "git-xxx"
	# and the child will fail.
	grep "d0|main|cmd_name|.*|_run_dashed_ (_run_dashed_)" actual &&
	grep "d0|main|child_start|.*|.* class:dashed argv:\[git-xxx\]" actual &&

	# We unpeel that and substitute "yyy" into "xxx" (giving "git yyy")
	# and spawn "git-yyy" and the child will fail.
	grep "d0|main|alias|.*|alias:xxx argv:\[yyy\]" actual &&
	grep "d0|main|cmd_name|.*|_run_dashed_ (_run_dashed_/_run_dashed_)" actual &&
	grep "d0|main|child_start|.*|.* class:dashed argv:\[git-yyy\]" actual &&

	# We unpeel that and substitute "version" into "xxx" (giving
	# "git version") and update the cmd_name event.
	grep "d0|main|alias|.*|alias:yyy argv:\[version\]" actual &&
	grep "d0|main|cmd_name|.*|_run_git_alias_ (_run_dashed_/_run_dashed_/_run_git_alias_)" actual &&

	# These def_param events could be associated with any of the
	# above cmd_name events.  It does not matter.
	grep "d0|main|def_param|.*|cfg.prop.foo:red" actual >actual.matches &&
	grep "d0|main|def_param|.*|ENV_PROP_FOO:blue" actual &&

	# However, we do not want them repeated each time we unpeel.
	test_line_count = 1 actual.matches &&

	# The "git version" child sees a different cmd_name hierarchy.
	# Also test the def_param (only for completeness).
	grep "d1|main|cmd_name|.*|version (_run_dashed_/_run_dashed_/_run_git_alias_/version)" actual &&
	grep "d1|main|def_param|.*|cfg.prop.foo:red" actual &&
	grep "d1|main|def_param|.*|ENV_PROP_FOO:blue" actual
'

test_done
