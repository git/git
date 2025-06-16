#!/bin/sh

test_description='test trace2 facility (normal target)'

. ./test-lib.sh

# Turn off any inherited trace2 settings for this test.
sane_unset GIT_TRACE2 GIT_TRACE2_PERF GIT_TRACE2_EVENT
sane_unset GIT_TRACE2_BRIEF
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
# This script tests the normal target in isolation.
#
# Defer setting GIT_TRACE2 until the actual command line we want to test
# because hidden git and test-tool commands run by the test harness
# can contaminate our output.

# Enable "brief" feature which turns off "<clock> <file>:<line> " prefix.
GIT_TRACE2_BRIEF=1 && export GIT_TRACE2_BRIEF

# Basic tests of the trace2 normal stream.  Since this stream is used
# primarily with printf-style debugging/tracing, we do limited testing
# here.
#
# We do confirm the following API features:
# [] the 'version <v>' event
# [] the 'start <argv>' event
# [] the 'cmd_name <name>' event
# [] the 'exit <time> code:<code>' event
# [] the 'atexit <time> code:<code>' event
#
# Fields of the form _FIELD_ are tokens that have been replaced (such
# as the elapsed time).

# Verb 001return
#
# Implicit return from cmd_<verb> function propagates <code>.

scrub_normal () {
	# Scrub the variable fields from the normal trace2 output to make
	# testing easier:
	#
	#   1. Various messages include an elapsed time in the middle of the
	#      message. Replace the time with a placeholder to simplify our
	#      HEREDOC in the test script.
	#
	#   2. We expect:
	#
	#        start <argv0> [<argv1> [<argv2> [...]]]
	#
	#      where argv0 might be a relative or absolute path, with or
	#      without quotes, and platform dependent. Replace argv0 with a
	#      token for HEREDOC matching in the test script.
	#
	#   3. Likewise, the 'cmd_path' message breaks out argv[0].
	#
	#      This line is only emitted when RUNTIME_PREFIX is defined,
	#      so just omit it for testing purposes.
	#
	#   4. 'cmd_ancestry' is not implemented everywhere, so for portability's
	#      sake, skip it when parsing normal.
	sed \
		-e 's/elapsed:[0-9]*\.[0-9][0-9]*\([eE][-+]\{0,1\}[0-9][0-9]*\)\{0,1\}/elapsed:_TIME_/g' \
		-e "s/^start '[^']*' \(.*\)/start _EXE_ \1/" \
		-e 's/^start [^ ][^ ]* \(.*\)/start _EXE_ \1/' \
		-e '/^cmd_path/d' \
		-e '/^cmd_ancestry/d'
}

test_expect_success 'normal stream, return code 0' '
	test_when_finished "rm trace.normal actual expect" &&
	GIT_TRACE2="$(pwd)/trace.normal" test-tool trace2 001return 0 &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 001return 0
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:0
		atexit elapsed:_TIME_ code:0
	EOF
	test_cmp expect actual
'

test_expect_success 'normal stream, return code 1' '
	test_when_finished "rm trace.normal actual expect" &&
	test_must_fail env GIT_TRACE2="$(pwd)/trace.normal" test-tool trace2 001return 1 &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 001return 1
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:1
		atexit elapsed:_TIME_ code:1
	EOF
	test_cmp expect actual
'

test_expect_success 'automatic filename' '
	test_when_finished "rm -r traces actual expect" &&
	mkdir traces &&
	GIT_TRACE2="$(pwd)/traces" test-tool trace2 001return 0 &&
	scrub_normal <"$(ls traces/*)" >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 001return 0
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:0
		atexit elapsed:_TIME_ code:0
	EOF
	test_cmp expect actual
'

# Verb 002exit
#
# Explicit exit(code) from within cmd_<verb> propagates <code>.

test_expect_success 'normal stream, exit code 0' '
	test_when_finished "rm trace.normal actual expect" &&
	GIT_TRACE2="$(pwd)/trace.normal" test-tool trace2 002exit 0 &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 002exit 0
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:0
		atexit elapsed:_TIME_ code:0
	EOF
	test_cmp expect actual
'

test_expect_success 'normal stream, exit code 1' '
	test_when_finished "rm trace.normal actual expect" &&
	test_must_fail env GIT_TRACE2="$(pwd)/trace.normal" test-tool trace2 002exit 1 &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 002exit 1
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:1
		atexit elapsed:_TIME_ code:1
	EOF
	test_cmp expect actual
'

# Verb 003error
#
# To the above, add multiple 'error <msg>' events

test_expect_success 'normal stream, error event' '
	test_when_finished "rm trace.normal actual expect" &&
	GIT_TRACE2="$(pwd)/trace.normal" test-tool trace2 003error "hello world" "this is a test" &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 003error '\''hello world'\'' '\''this is a test'\''
		cmd_name trace2 (trace2)
		error hello world
		error this is a test
		exit elapsed:_TIME_ code:0
		atexit elapsed:_TIME_ code:0
	EOF
	test_cmp expect actual
'

# Verb 007bug
#
# Check that BUG writes to trace2

test_expect_success 'BUG messages are written to trace2' '
	test_when_finished "rm trace.normal actual expect" &&
	test_must_fail env GIT_TRACE2="$(pwd)/trace.normal" test-tool trace2 007bug &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 007bug
		cmd_name trace2 (trace2)
		error the bug message
		exit elapsed:_TIME_ code:99
		atexit elapsed:_TIME_ code:99
	EOF
	test_cmp expect actual
'

test_expect_success 'bug messages with BUG_if_bug() are written to trace2' '
	test_when_finished "rm trace.normal actual expect" &&
	test_expect_code 99 env GIT_TRACE2="$(pwd)/trace.normal" \
		test-tool trace2 008bug 2>err &&
	cat >expect <<-\EOF &&
	a bug message
	another bug message
	an explicit BUG_if_bug() following bug() call(s) is nice, but not required
	EOF
	sed "s/^.*: //" <err >actual &&
	test_cmp expect actual &&

	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 008bug
		cmd_name trace2 (trace2)
		error a bug message
		error another bug message
		error an explicit BUG_if_bug() following bug() call(s) is nice, but not required
		exit elapsed:_TIME_ code:99
		atexit elapsed:_TIME_ code:99
	EOF
	test_cmp expect actual
'

test_expect_success 'bug messages without explicit BUG_if_bug() are written to trace2' '
	test_when_finished "rm trace.normal actual expect" &&
	test_expect_code 99 env GIT_TRACE2="$(pwd)/trace.normal" \
		test-tool trace2 009bug_BUG 2>err &&
	cat >expect <<-\EOF &&
	a bug message
	another bug message
	had bug() call(s) in this process without explicit BUG_if_bug()
	EOF
	sed "s/^.*: //" <err >actual &&
	test_cmp expect actual &&

	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 009bug_BUG
		cmd_name trace2 (trace2)
		error a bug message
		error another bug message
		error on exit(): had bug() call(s) in this process without explicit BUG_if_bug()
		exit elapsed:_TIME_ code:99
		atexit elapsed:_TIME_ code:99
	EOF
	test_cmp expect actual
'

test_expect_success 'bug messages followed by BUG() are written to trace2' '
	test_when_finished "rm trace.normal actual expect" &&
	test_expect_code 99 env GIT_TRACE2="$(pwd)/trace.normal" \
		test-tool trace2 010bug_BUG 2>err &&
	cat >expect <<-\EOF &&
	a bug message
	a BUG message
	EOF
	sed "s/^.*: //" <err >actual &&
	test_cmp expect actual &&

	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 010bug_BUG
		cmd_name trace2 (trace2)
		error a bug message
		error a BUG message
		exit elapsed:_TIME_ code:99
		atexit elapsed:_TIME_ code:99
	EOF
	test_cmp expect actual
'

test_expect_success 'a valueless true configuration variable is handled' '
	test_when_finished "rm -f trace2.normal actual expect" &&
	echo >expect &&
	GIT_TRACE2="$(pwd)/trace2.normal" \
	GIT_TRACE2_CONFIG_PARAMS=foo.true \
	git -c foo.true config foo.true >actual &&
	test_cmp expect actual
'

sane_unset GIT_TRACE2_BRIEF

# Now test without environment variables and get all Trace2 settings
# from the global config.

test_expect_success 'using global config, normal stream, return code 0' '
	test_when_finished "rm trace.normal actual expect" &&
	test_config_global trace2.normalBrief 1 &&
	test_config_global trace2.normalTarget "$(pwd)/trace.normal" &&
	test-tool trace2 001return 0 &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 001return 0
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:0
		atexit elapsed:_TIME_ code:0
	EOF
	test_cmp expect actual
'

test_expect_success 'using global config with include' '
	test_when_finished "rm trace.normal actual expect real.gitconfig" &&
	test_config_global trace2.normalBrief 1 &&
	test_config_global trace2.normalTarget "$(pwd)/trace.normal" &&
	mv "$(pwd)/.gitconfig" "$(pwd)/real.gitconfig" &&
	test_config_global include.path "$(pwd)/real.gitconfig" &&
	test-tool trace2 001return 0 &&
	scrub_normal <trace.normal >actual &&
	cat >expect <<-EOF &&
		version $V
		start _EXE_ trace2 001return 0
		cmd_name trace2 (trace2)
		exit elapsed:_TIME_ code:0
		atexit elapsed:_TIME_ code:0
	EOF
	test_cmp expect actual
'

test_expect_success 'unsafe URLs are redacted by default' '
	test_when_finished \
		"rm -r trace.normal unredacted.normal clone clone2" &&

	test_config_global \
		"url.$(pwd).insteadOf" https://user:pwd@example.com/ &&
	test_config_global trace2.configParams "core.*,remote.*.url" &&

	GIT_TRACE2="$(pwd)/trace.normal" \
		git clone https://user:pwd@example.com/ clone &&
	! grep user:pwd trace.normal &&

	GIT_TRACE2_REDACT=0 GIT_TRACE2="$(pwd)/unredacted.normal" \
		git clone https://user:pwd@example.com/ clone2 &&
	grep "start .* clone https://user:pwd@example.com" unredacted.normal &&
	grep "remote.origin.url=https://user:pwd@example.com" unredacted.normal
'

test_done
