#!/bin/sh

test_description='test trace2 facility'

. ./test-lib.sh

# Turn off any inherited trace2 settings for this test.
sane_unset GIT_TRACE2 GIT_TRACE2_PERF GIT_TRACE2_EVENT
sane_unset GIT_TRACE2_BARE
sane_unset GIT_TRACE2_CONFIG_PARAMS

perl -MJSON::PP -e 0 >/dev/null 2>&1 && test_set_prereq JSON_PP

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

# We don't bother repeating the 001return and 002exit tests, since they
# have coverage in the normal and perf targets.

# Verb 003error
#
# To the above, add multiple 'error <msg>' events

test_expect_success JSON_PP 'event stream, error event' '
	test_when_finished "rm trace.event actual expect" &&
	GIT_TRACE2_EVENT="$(pwd)/trace.event" test-tool trace2 003error "hello world" "this is a test" &&
	perl "$TEST_DIRECTORY/t0212/parse_events.perl" <trace.event >actual &&
	sed -e "s/^|//" >expect <<-EOF &&
	|VAR1 = {
	|  "_SID0_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "003error",
	|      "hello world",
	|      "this is a test"
	|    ],
	|    "errors":[
	|      "%s",
	|      "%s"
	|    ],
	|    "exit_code":0,
	|    "hierarchy":"trace2",
	|    "name":"trace2",
	|    "version":"$V"
	|  }
	|};
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

test_expect_success JSON_PP 'event stream, return code 0' '
	test_when_finished "rm trace.event actual expect" &&
	GIT_TRACE2_EVENT="$(pwd)/trace.event" test-tool trace2 004child test-tool trace2 004child test-tool trace2 001return 0 &&
	perl "$TEST_DIRECTORY/t0212/parse_events.perl" <trace.event >actual &&
	sed -e "s/^|//" >expect <<-EOF &&
	|VAR1 = {
	|  "_SID0_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "004child",
	|      "test-tool",
	|      "trace2",
	|      "004child",
	|      "test-tool",
	|      "trace2",
	|      "001return",
	|      "0"
	|    ],
	|    "child":{
	|      "0":{
	|        "child_argv":[
	|          "_EXE_",
	|          "trace2",
	|          "004child",
	|          "test-tool",
	|          "trace2",
	|          "001return",
	|          "0"
	|        ],
	|        "child_class":"?",
	|        "child_code":0,
	|        "use_shell":0
	|      }
	|    },
	|    "exit_code":0,
	|    "hierarchy":"trace2",
	|    "name":"trace2",
	|    "version":"$V"
	|  },
	|  "_SID0_/_SID1_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "004child",
	|      "test-tool",
	|      "trace2",
	|      "001return",
	|      "0"
	|    ],
	|    "child":{
	|      "0":{
	|        "child_argv":[
	|          "_EXE_",
	|          "trace2",
	|          "001return",
	|          "0"
	|        ],
	|        "child_class":"?",
	|        "child_code":0,
	|        "use_shell":0
	|      }
	|    },
	|    "exit_code":0,
	|    "hierarchy":"trace2/trace2",
	|    "name":"trace2",
	|    "version":"$V"
	|  },
	|  "_SID0_/_SID1_/_SID2_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "001return",
	|      "0"
	|    ],
	|    "exit_code":0,
	|    "hierarchy":"trace2/trace2/trace2",
	|    "name":"trace2",
	|    "version":"$V"
	|  }
	|};
	EOF
	test_cmp expect actual
'

# Test listing of all "interesting" config settings.

test_expect_success JSON_PP 'event stream, list config' '
	test_when_finished "rm trace.event actual expect" &&
	git config --local t0212.abc 1 &&
	git config --local t0212.def "hello world" &&
	GIT_TRACE2_EVENT="$(pwd)/trace.event" GIT_TRACE2_CONFIG_PARAMS="t0212.*" test-tool trace2 001return 0 &&
	perl "$TEST_DIRECTORY/t0212/parse_events.perl" <trace.event >actual &&
	sed -e "s/^|//" >expect <<-EOF &&
	|VAR1 = {
	|  "_SID0_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "001return",
	|      "0"
	|    ],
	|    "exit_code":0,
	|    "hierarchy":"trace2",
	|    "name":"trace2",
	|    "params":[
	|      {
	|        "param":"t0212.abc",
	|        "value":"1"
	|      },
	|      {
	|        "param":"t0212.def",
	|        "value":"hello world"
	|      }
	|    ],
	|    "version":"$V"
	|  }
	|};
	EOF
	test_cmp expect actual
'

# Test listing of all "interesting" environment variables.

test_expect_success JSON_PP 'event stream, list env vars' '
	test_when_finished "rm trace.event actual expect" &&
	GIT_TRACE2_EVENT="$(pwd)/trace.event" \
		GIT_TRACE2_ENV_VARS="A_VAR,OTHER_VAR,MISSING" \
		A_VAR=1 OTHER_VAR="hello world" test-tool trace2 001return 0 &&
	perl "$TEST_DIRECTORY/t0212/parse_events.perl" <trace.event >actual &&
	sed -e "s/^|//" >expect <<-EOF &&
	|VAR1 = {
	|  "_SID0_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "001return",
	|      "0"
	|    ],
	|    "exit_code":0,
	|    "hierarchy":"trace2",
	|    "name":"trace2",
	|    "params":[
	|      {
	|        "param":"A_VAR",
	|        "value":"1"
	|      },
	|      {
	|        "param":"OTHER_VAR",
	|        "value":"hello world"
	|      }
	|    ],
	|    "version":"$V"
	|  }
	|};
	EOF
	test_cmp expect actual
'

test_expect_success JSON_PP 'basic trace2_data' '
	test_when_finished "rm trace.event actual expect" &&
	GIT_TRACE2_EVENT="$(pwd)/trace.event" test-tool trace2 006data test_category k1 v1 test_category k2 v2 &&
	perl "$TEST_DIRECTORY/t0212/parse_events.perl" <trace.event >actual &&
	sed -e "s/^|//" >expect <<-EOF &&
	|VAR1 = {
	|  "_SID0_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "006data",
	|      "test_category",
	|      "k1",
	|      "v1",
	|      "test_category",
	|      "k2",
	|      "v2"
	|    ],
	|    "data":{
	|      "test_category":{
	|        "k1":"v1",
	|        "k2":"v2"
	|      }
	|    },
	|    "exit_code":0,
	|    "hierarchy":"trace2",
	|    "name":"trace2",
	|    "version":"$V"
	|  }
	|};
	EOF
	test_cmp expect actual
'

# Now test without environment variables and get all Trace2 settings
# from the global config.

test_expect_success JSON_PP 'using global config, event stream, error event' '
	test_when_finished "rm trace.event actual expect" &&
	test_config_global trace2.eventTarget "$(pwd)/trace.event" &&
	test-tool trace2 003error "hello world" "this is a test" &&
	perl "$TEST_DIRECTORY/t0212/parse_events.perl" <trace.event >actual &&
	sed -e "s/^|//" >expect <<-EOF &&
	|VAR1 = {
	|  "_SID0_":{
	|    "argv":[
	|      "_EXE_",
	|      "trace2",
	|      "003error",
	|      "hello world",
	|      "this is a test"
	|    ],
	|    "errors":[
	|      "%s",
	|      "%s"
	|    ],
	|    "exit_code":0,
	|    "hierarchy":"trace2",
	|    "name":"trace2",
	|    "version":"$V"
	|  }
	|};
	EOF
	test_cmp expect actual
'

test_expect_success 'discard traces when there are too many files' '
	mkdir trace_target_dir &&
	test_when_finished "rm -r trace_target_dir" &&
	(
		GIT_TRACE2_MAX_FILES=5 &&
		export GIT_TRACE2_MAX_FILES &&
		cd trace_target_dir &&
		test_seq $GIT_TRACE2_MAX_FILES >../expected_filenames.txt &&
		xargs touch <../expected_filenames.txt &&
		cd .. &&
		GIT_TRACE2_EVENT="$(pwd)/trace_target_dir" test-tool trace2 001return 0
	) &&
	echo git-trace2-discard >>expected_filenames.txt &&
	ls trace_target_dir >ls_output.txt &&
	test_cmp expected_filenames.txt ls_output.txt &&
	head -n1 trace_target_dir/git-trace2-discard | grep \"event\":\"version\" &&
	head -n2 trace_target_dir/git-trace2-discard | tail -n1 | grep \"event\":\"too_many_files\"
'

# In the following "...redact..." tests, skip testing the GIT_TRACE2_REDACT=0
# case because we would need to exactly model the full JSON event stream like
# we did in the basic tests above and I do not think it is worth it.

test_expect_success 'unsafe URLs are redacted by default in cmd_start events' '
	test_when_finished \
		"rm -r trace.event" &&

	GIT_TRACE2_EVENT="$(pwd)/trace.event" \
		test-tool trace2 300redact_start git clone https://user:pwd@example.com/ clone2 &&
	! grep user:pwd trace.event
'

test_expect_success 'unsafe URLs are redacted by default in child_start events' '
	test_when_finished \
		"rm -r trace.event" &&

	GIT_TRACE2_EVENT="$(pwd)/trace.event" \
		test-tool trace2 301redact_child_start git clone https://user:pwd@example.com/ clone2 &&
	! grep user:pwd trace.event
'

test_expect_success 'unsafe URLs are redacted by default in exec events' '
	test_when_finished \
		"rm -r trace.event" &&

	GIT_TRACE2_EVENT="$(pwd)/trace.event" \
		test-tool trace2 302redact_exec git clone https://user:pwd@example.com/ clone2 &&
	! grep user:pwd trace.event
'

test_expect_success 'unsafe URLs are redacted by default in def_param events' '
	test_when_finished \
		"rm -r trace.event" &&

	GIT_TRACE2_EVENT="$(pwd)/trace.event" \
		test-tool trace2 303redact_def_param url https://user:pwd@example.com/ &&
	! grep user:pwd trace.event
'

test_done
