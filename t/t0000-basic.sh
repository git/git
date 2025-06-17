#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test the very basics part #1.

The rest of the test suite does not check the basic operation of git
plumbing commands to work very carefully.  Their job is to concentrate
on tricky features that caused bugs in the past to detect regression.

This test runs very basic features, like registering things in cache,
writing tree, etc.

Note that this test *deliberately* hard-codes many expected object
IDs.  When object ID computation changes, like in the previous case of
swapping compression and hashing order, the person who is making the
modification *should* take notice and update the test vectors here.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-subtest.sh

try_local_xy () {
	local x="local" y="alsolocal" &&
	echo "$x $y"
}

# Check whether the shell supports the "local" keyword. "local" is not
# POSIX-standard, but it is very widely supported by POSIX-compliant
# shells, and we rely on it within Git's test framework.
#
# If your shell fails this test, the results of other tests may be
# unreliable. You may wish to report the problem to the Git mailing
# list <git@vger.kernel.org>, as it could cause us to reconsider
# relying on "local".
test_expect_success 'verify that the running shell supports "local"' '
	x="notlocal" &&
	y="alsonotlocal" &&
	echo "local alsolocal" >expected1 &&
	try_local_xy >actual1 &&
	test_cmp expected1 actual1 &&
	echo "notlocal alsonotlocal" >expected2 &&
	echo "$x $y" >actual2 &&
	test_cmp expected2 actual2
'

################################################################
# git init has been done in an empty repository.
# make sure it is empty.

test_expect_success '.git/objects should be empty after git init in an empty repo' '
	find .git/objects -type f -print >should-be-empty &&
	test_line_count = 0 should-be-empty
'

# also it should have 2 subdirectories; no fan-out anymore, pack, and info.
# 3 is counting "objects" itself
test_expect_success '.git/objects should have 3 subdirectories' '
	find .git/objects -type d -print >full-of-directories &&
	test_line_count = 3 full-of-directories
'

################################################################
# Test harness
test_expect_success 'success is reported like this' '
	:
'

test_expect_success 'subtest: 3 passing tests' '
	write_and_run_sub_test_lib_test full-pass <<-\EOF &&
	for i in 1 2 3
	do
		test_expect_success "passing test #$i" "true"
	done
	test_done
	EOF
	check_sub_test_lib_test full-pass <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 - passing test #3
	> # passed all 3 test(s)
	> 1..3
	EOF
'

test_expect_success 'subtest: 2/3 tests passing' '
	write_and_run_sub_test_lib_test_err partial-pass <<-\EOF &&
	test_expect_success "passing test #1" "true"
	test_expect_success "failing test #2" "false"
	test_expect_success "passing test #3" "true"
	test_done
	EOF
	check_sub_test_lib_test partial-pass <<-\EOF
	> ok 1 - passing test #1
	> not ok 2 - failing test #2
	#	false
	> ok 3 - passing test #3
	> # failed 1 among 3 test(s)
	> 1..3
	EOF
'

test_expect_success 'subtest: --immediate' '
	run_sub_test_lib_test_err partial-pass \
		--immediate &&
	check_sub_test_lib_test_err partial-pass \
		<<-\EOF_OUT 3<<-EOF_ERR
	> ok 1 - passing test #1
	> not ok 2 - failing test #2
	> #	false
	> 1..2
	EOF_OUT
	EOF_ERR
'

test_expect_success 'subtest: a failing TODO test' '
	write_and_run_sub_test_lib_test failing-todo <<-\EOF &&
	test_expect_success "passing test" "true"
	test_expect_failure "pretend we have a known breakage" "false"
	test_done
	EOF
	check_sub_test_lib_test failing-todo <<-\EOF
	> ok 1 - passing test
	> not ok 2 - pretend we have a known breakage # TODO known breakage
	> # still have 1 known breakage(s)
	> # passed all remaining 1 test(s)
	> 1..2
	EOF
'

test_expect_success 'subtest: a passing TODO test' '
	write_and_run_sub_test_lib_test_err passing-todo <<-\EOF &&
	test_expect_failure "pretend we have fixed a known breakage" "true"
	test_done
	EOF
	check_sub_test_lib_test passing-todo <<-\EOF
	> ok 1 - pretend we have fixed a known breakage # TODO known breakage vanished
	> # 1 known breakage(s) vanished; please update test(s)
	> 1..1
	EOF
'

test_expect_success 'subtest: 2 TODO tests, one passin' '
	write_and_run_sub_test_lib_test_err partially-passing-todos <<-\EOF &&
	test_expect_failure "pretend we have a known breakage" "false"
	test_expect_success "pretend we have a passing test" "true"
	test_expect_failure "pretend we have fixed another known breakage" "true"
	test_done
	EOF
	check_sub_test_lib_test partially-passing-todos <<-\EOF
	> not ok 1 - pretend we have a known breakage # TODO known breakage
	> ok 2 - pretend we have a passing test
	> ok 3 - pretend we have fixed another known breakage # TODO known breakage vanished
	> # 1 known breakage(s) vanished; please update test(s)
	> # still have 1 known breakage(s)
	> # passed all remaining 1 test(s)
	> 1..3
	EOF
'

test_expect_success 'subtest: mixed results: pass, failure and a TODO test' '
	write_and_run_sub_test_lib_test_err mixed-results1 <<-\EOF &&
	test_expect_success "passing test" "true"
	test_expect_success "failing test" "false"
	test_expect_failure "pretend we have a known breakage" "false"
	test_done
	EOF
	check_sub_test_lib_test mixed-results1 <<-\EOF
	> ok 1 - passing test
	> not ok 2 - failing test
	> #	false
	> not ok 3 - pretend we have a known breakage # TODO known breakage
	> # still have 1 known breakage(s)
	> # failed 1 among remaining 2 test(s)
	> 1..3
	EOF
'

test_expect_success 'subtest: mixed results: a mixture of all possible results' '
	write_and_run_sub_test_lib_test_err mixed-results2 <<-\EOF &&
	test_expect_success "passing test" "true"
	test_expect_success "passing test" "true"
	test_expect_success "passing test" "true"
	test_expect_success "passing test" "true"
	test_expect_success "failing test" "false"
	test_expect_success "failing test" "false"
	test_expect_success "failing test" "false"
	test_expect_failure "pretend we have a known breakage" "false"
	test_expect_failure "pretend we have a known breakage" "false"
	test_expect_failure "pretend we have fixed a known breakage" "true"
	test_done
	EOF
	check_sub_test_lib_test mixed-results2 <<-\EOF
	> ok 1 - passing test
	> ok 2 - passing test
	> ok 3 - passing test
	> ok 4 - passing test
	> not ok 5 - failing test
	> #	false
	> not ok 6 - failing test
	> #	false
	> not ok 7 - failing test
	> #	false
	> not ok 8 - pretend we have a known breakage # TODO known breakage
	> not ok 9 - pretend we have a known breakage # TODO known breakage
	> ok 10 - pretend we have fixed a known breakage # TODO known breakage vanished
	> # 1 known breakage(s) vanished; please update test(s)
	> # still have 2 known breakage(s)
	> # failed 3 among remaining 7 test(s)
	> 1..10
	EOF
'

test_expect_success 'subtest: --verbose option' '
	write_and_run_sub_test_lib_test_err t1234-verbose --verbose <<-\EOF &&
	test_expect_success "passing test" true
	test_expect_success "test with output" "echo foo"
	test_expect_success "failing test" false
	test_done
	EOF
	mv t1234-verbose/err t1234-verbose/err+ &&
	grep -v "^Initialized empty" t1234-verbose/err+ >t1234-verbose/err &&
	check_sub_test_lib_test_err t1234-verbose \
		<<-\EOF_OUT 3<<-\EOF_ERR
	> ok 1 - passing test
	> ok 2 - test with output
	> not ok 3 - failing test
	> #	false
	> # failed 1 among 3 test(s)
	> 1..3
	EOF_OUT
	> expecting success of 1234.1 '\''passing test'\'': true
	> Z
	> expecting success of 1234.2 '\''test with output'\'': echo foo
	> foo
	> Z
	> expecting success of 1234.3 '\''failing test'\'': false
	> Z
	EOF_ERR
'

test_expect_success 'subtest: --verbose-only option' '
	run_sub_test_lib_test_err \
		t1234-verbose \
		--verbose-only=2 &&
	check_sub_test_lib_test_err t1234-verbose <<-\EOF_OUT 3<<-\EOF_ERR
	> ok 1 - passing test
	> ok 2 - test with output
	> not ok 3 - failing test
	> #	false
	> # failed 1 among 3 test(s)
	> 1..3
	EOF_OUT
	> Z
	> expecting success of 1234.2 '\''test with output'\'': echo foo
	> foo
	> Z
	EOF_ERR
'

test_expect_success 'subtest: skip one with GIT_SKIP_TESTS' '
	(
		run_sub_test_lib_test full-pass \
			--skip="full.2" &&
		check_sub_test_lib_test full-pass <<-\EOF
		> ok 1 - passing test #1
		> ok 2 # skip passing test #2 (GIT_SKIP_TESTS)
		> ok 3 - passing test #3
		> # passed all 3 test(s)
		> 1..3
		EOF
	)
'

test_expect_success 'subtest: skip several with GIT_SKIP_TESTS' '
	(
		write_and_run_sub_test_lib_test git-skip-tests-several \
			--skip="git.2 git.5" <<-\EOF &&
		for i in 1 2 3 4 5 6
		do
			test_expect_success "passing test #$i" "true"
		done
		test_done
		EOF
		check_sub_test_lib_test git-skip-tests-several <<-\EOF
		> ok 1 - passing test #1
		> ok 2 # skip passing test #2 (GIT_SKIP_TESTS)
		> ok 3 - passing test #3
		> ok 4 - passing test #4
		> ok 5 # skip passing test #5 (GIT_SKIP_TESTS)
		> ok 6 - passing test #6
		> # passed all 6 test(s)
		> 1..6
		EOF
	)
'

test_expect_success 'subtest: sh pattern skipping with GIT_SKIP_TESTS' '
	(
		run_sub_test_lib_test git-skip-tests-several \
			--skip="git.[2-5]" &&
		check_sub_test_lib_test git-skip-tests-several <<-\EOF
		> ok 1 - passing test #1
		> ok 2 # skip passing test #2 (GIT_SKIP_TESTS)
		> ok 3 # skip passing test #3 (GIT_SKIP_TESTS)
		> ok 4 # skip passing test #4 (GIT_SKIP_TESTS)
		> ok 5 # skip passing test #5 (GIT_SKIP_TESTS)
		> ok 6 - passing test #6
		> # passed all 6 test(s)
		> 1..6
		EOF
	)
'

test_expect_success 'subtest: skip entire test suite with GIT_SKIP_TESTS' '
	(
		GIT_SKIP_TESTS="git" && export GIT_SKIP_TESTS &&
		run_sub_test_lib_test git-skip-tests-several \
			--skip="git" &&
		check_sub_test_lib_test git-skip-tests-several <<-\EOF
		> 1..0 # SKIP skip all tests in git
		EOF
	)
'

test_expect_success 'subtest: GIT_SKIP_TESTS does not skip unmatched suite' '
	(
		GIT_SKIP_TESTS="notgit" && export GIT_SKIP_TESTS &&
		run_sub_test_lib_test full-pass \
			--skip="notfull" &&
		check_sub_test_lib_test full-pass <<-\EOF
		> ok 1 - passing test #1
		> ok 2 - passing test #2
		> ok 3 - passing test #3
		> # passed all 3 test(s)
		> 1..3
		EOF
	)
'

test_expect_success 'subtest: --run basic' '
	run_sub_test_lib_test git-skip-tests-several --run="1,3,5" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 # skip passing test #2 (--run)
	> ok 3 - passing test #3
	> ok 4 # skip passing test #4 (--run)
	> ok 5 - passing test #5
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run with a range' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="1-3" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 - passing test #3
	> ok 4 # skip passing test #4 (--run)
	> ok 5 # skip passing test #5 (--run)
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run with two ranges' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="1-2,5-6" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 # skip passing test #3 (--run)
	> ok 4 # skip passing test #4 (--run)
	> ok 5 - passing test #5
	> ok 6 - passing test #6
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run with a left open range' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="-3" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 - passing test #3
	> ok 4 # skip passing test #4 (--run)
	> ok 5 # skip passing test #5 (--run)
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run with a right open range' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="4-" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 # skip passing test #1 (--run)
	> ok 2 # skip passing test #2 (--run)
	> ok 3 # skip passing test #3 (--run)
	> ok 4 - passing test #4
	> ok 5 - passing test #5
	> ok 6 - passing test #6
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run with basic negation' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="!3" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 # skip passing test #3 (--run)
	> ok 4 - passing test #4
	> ok 5 - passing test #5
	> ok 6 - passing test #6
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run with two negations' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="!3,!6" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 # skip passing test #3 (--run)
	> ok 4 - passing test #4
	> ok 5 - passing test #5
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run a range and negation' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="-4,!2" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 # skip passing test #2 (--run)
	> ok 3 - passing test #3
	> ok 4 - passing test #4
	> ok 5 # skip passing test #5 (--run)
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run range negation' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="!1-3" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 # skip passing test #1 (--run)
	> ok 2 # skip passing test #2 (--run)
	> ok 3 # skip passing test #3 (--run)
	> ok 4 - passing test #4
	> ok 5 - passing test #5
	> ok 6 - passing test #6
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run include, exclude and include' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="1-5,!1-3,2" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 # skip passing test #1 (--run)
	> ok 2 - passing test #2
	> ok 3 # skip passing test #3 (--run)
	> ok 4 - passing test #4
	> ok 5 - passing test #5
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run include, exclude and include, comma separated' '
	run_sub_test_lib_test git-skip-tests-several \
		--run=1-5,!1-3,2 &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 # skip passing test #1 (--run)
	> ok 2 - passing test #2
	> ok 3 # skip passing test #3 (--run)
	> ok 4 - passing test #4
	> ok 5 - passing test #5
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run exclude and include' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="!3-,5" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 - passing test #2
	> ok 3 # skip passing test #3 (--run)
	> ok 4 # skip passing test #4 (--run)
	> ok 5 - passing test #5
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run empty selectors' '
	run_sub_test_lib_test git-skip-tests-several \
		--run="1,,3,,,5" &&
	check_sub_test_lib_test git-skip-tests-several <<-\EOF
	> ok 1 - passing test #1
	> ok 2 # skip passing test #2 (--run)
	> ok 3 - passing test #3
	> ok 4 # skip passing test #4 (--run)
	> ok 5 - passing test #5
	> ok 6 # skip passing test #6 (--run)
	> # passed all 6 test(s)
	> 1..6
	EOF
'

test_expect_success 'subtest: --run substring selector' '
	write_and_run_sub_test_lib_test run-substring-selector \
		--run="relevant" <<-\EOF &&
	test_expect_success "relevant test" "true"
	for i in 1 2 3 4 5 6
	do
		test_expect_success "other test #$i" "true"
	done
	test_done
	EOF
	check_sub_test_lib_test run-substring-selector <<-\EOF
	> ok 1 - relevant test
	> ok 2 # skip other test #1 (--run)
	> ok 3 # skip other test #2 (--run)
	> ok 4 # skip other test #3 (--run)
	> ok 5 # skip other test #4 (--run)
	> ok 6 # skip other test #5 (--run)
	> ok 7 # skip other test #6 (--run)
	> # passed all 7 test(s)
	> 1..7
	EOF
'

test_expect_success 'subtest: --run keyword selection' '
	write_and_run_sub_test_lib_test_err run-inv-range-start \
		--run="a-5" <<-\EOF &&
	test_expect_success "passing test #1" "true"
	test_done
	EOF
	check_sub_test_lib_test_err run-inv-range-start \
		<<-\EOF_OUT 3<<-EOF_ERR
	> FATAL: Unexpected exit with code 1
	EOF_OUT
	> error: --run: invalid non-numeric in range start: ${SQ}a-5${SQ}
	EOF_ERR
'

test_expect_success 'subtest: --run invalid range end' '
	run_sub_test_lib_test_err run-inv-range-start \
		--run="1-z" &&
	check_sub_test_lib_test_err run-inv-range-start \
		<<-\EOF_OUT 3<<-EOF_ERR
	> FATAL: Unexpected exit with code 1
	EOF_OUT
	> error: --run: invalid non-numeric in range end: ${SQ}1-z${SQ}
	EOF_ERR
'

test_expect_success 'subtest: --invert-exit-code without --immediate' '
	run_sub_test_lib_test_err full-pass \
		--invert-exit-code &&
	check_sub_test_lib_test_err full-pass \
		<<-\EOF_OUT 3<<-EOF_ERR
	ok 1 - passing test #1
	ok 2 - passing test #2
	ok 3 - passing test #3
	# passed all 3 test(s)
	1..3
	# faking up non-zero exit with --invert-exit-code
	EOF_OUT
	EOF_ERR
'

test_expect_success 'subtest: --invert-exit-code with --immediate: all passed' '
	run_sub_test_lib_test_err full-pass \
		--invert-exit-code --immediate &&
	check_sub_test_lib_test_err full-pass \
		<<-\EOF_OUT 3<<-EOF_ERR
	ok 1 - passing test #1
	ok 2 - passing test #2
	ok 3 - passing test #3
	# passed all 3 test(s)
	1..3
	# faking up non-zero exit with --invert-exit-code
	EOF_OUT
	EOF_ERR
'

test_expect_success 'subtest: --invert-exit-code without --immediate: partial pass' '
	run_sub_test_lib_test partial-pass \
		--invert-exit-code &&
	check_sub_test_lib_test partial-pass <<-\EOF
	ok 1 - passing test #1
	not ok 2 - # TODO induced breakage (--invert-exit-code): failing test #2
	#	false
	ok 3 - passing test #3
	# failed 1 among 3 test(s)
	1..3
	# faked up failures as TODO & now exiting with 0 due to --invert-exit-code
	EOF
'

test_expect_success 'subtest: --invert-exit-code with --immediate: partial pass' '
	run_sub_test_lib_test partial-pass \
		--invert-exit-code --immediate &&
	check_sub_test_lib_test partial-pass \
		<<-\EOF_OUT 3<<-EOF_ERR
	ok 1 - passing test #1
	not ok 2 - # TODO induced breakage (--invert-exit-code): failing test #2
	#	false
	1..2
	# faked up failures as TODO & now exiting with 0 due to --invert-exit-code
	EOF_OUT
	EOF_ERR
'

test_expect_success 'subtest: --invert-exit-code --immediate: got a failure' '
	run_sub_test_lib_test partial-pass \
		--invert-exit-code --immediate &&
	check_sub_test_lib_test_err partial-pass \
		<<-\EOF_OUT 3<<-EOF_ERR
	ok 1 - passing test #1
	not ok 2 - # TODO induced breakage (--invert-exit-code): failing test #2
	#	false
	1..2
	# faked up failures as TODO & now exiting with 0 due to --invert-exit-code
	EOF_OUT
	EOF_ERR
'

test_expect_success 'subtest: tests respect prerequisites' '
	write_and_run_sub_test_lib_test prereqs <<-\EOF &&

	test_set_prereq HAVEIT
	test_expect_success HAVEIT "prereq is satisfied" "true"
	test_expect_success "have_prereq works" "
		test_have_prereq HAVEIT
	"
	test_expect_success DONTHAVEIT "prereq not satisfied" "false"

	test_set_prereq HAVETHIS
	test_expect_success HAVETHIS,HAVEIT "multiple prereqs" "true"
	test_expect_success HAVEIT,DONTHAVEIT "mixed prereqs (yes,no)" "false"
	test_expect_success DONTHAVEIT,HAVEIT "mixed prereqs (no,yes)" "false"

	test_done
	EOF

	check_sub_test_lib_test prereqs <<-\EOF
	ok 1 - prereq is satisfied
	ok 2 - have_prereq works
	ok 3 # skip prereq not satisfied (missing DONTHAVEIT)
	ok 4 - multiple prereqs
	ok 5 # skip mixed prereqs (yes,no) (missing DONTHAVEIT of HAVEIT,DONTHAVEIT)
	ok 6 # skip mixed prereqs (no,yes) (missing DONTHAVEIT of DONTHAVEIT,HAVEIT)
	# passed all 6 test(s)
	1..6
	EOF
'

test_expect_success 'subtest: tests respect lazy prerequisites' '
	write_and_run_sub_test_lib_test lazy-prereqs <<-\EOF &&

	test_lazy_prereq LAZY_TRUE true
	test_expect_success LAZY_TRUE "lazy prereq is satisfied" "true"
	test_expect_success !LAZY_TRUE "negative lazy prereq" "false"

	test_lazy_prereq LAZY_FALSE false
	test_expect_success LAZY_FALSE "lazy prereq not satisfied" "false"
	test_expect_success !LAZY_FALSE "negative false prereq" "true"

	test_done
	EOF

	check_sub_test_lib_test lazy-prereqs <<-\EOF
	ok 1 - lazy prereq is satisfied
	ok 2 # skip negative lazy prereq (missing !LAZY_TRUE)
	ok 3 # skip lazy prereq not satisfied (missing LAZY_FALSE)
	ok 4 - negative false prereq
	# passed all 4 test(s)
	1..4
	EOF
'

test_expect_success 'subtest: nested lazy prerequisites' '
	write_and_run_sub_test_lib_test nested-lazy <<-\EOF &&

	test_lazy_prereq NESTED_INNER "
		>inner &&
		rm -f outer
	"
	test_lazy_prereq NESTED_PREREQ "
		>outer &&
		test_have_prereq NESTED_INNER &&
		echo can create new file in cwd >file &&
		test_path_is_file outer &&
		test_path_is_missing inner
	"
	test_expect_success NESTED_PREREQ "evaluate nested prereq" "true"

	test_done
	EOF

	check_sub_test_lib_test nested-lazy <<-\EOF
	ok 1 - evaluate nested prereq
	# passed all 1 test(s)
	1..1
	EOF
'

test_expect_success 'subtest: lazy prereqs do not turn off tracing' '
	write_and_run_sub_test_lib_test lazy-prereq-and-tracing \
		-v -x <<-\EOF &&
	test_lazy_prereq LAZY true

	test_expect_success lazy "test_have_prereq LAZY && echo trace"

	test_done
	EOF

	grep "echo trace" lazy-prereq-and-tracing/err
'

test_expect_success 'subtest: tests clean up after themselves' '
	write_and_run_sub_test_lib_test cleanup <<-\EOF &&
	clean=no
	test_expect_success "do cleanup" "
		test_when_finished clean=yes
	"
	test_expect_success "cleanup happened" "
		test $clean = yes
	"
	test_done
	EOF

	check_sub_test_lib_test cleanup <<-\EOF
	ok 1 - do cleanup
	ok 2 - cleanup happened
	# passed all 2 test(s)
	1..2
	EOF
'

test_expect_success 'subtest: tests clean up even on failures' '
	write_and_run_sub_test_lib_test_err \
		failing-cleanup <<-\EOF &&
	test_expect_success "tests clean up even after a failure" "
		touch clean-after-failure &&
		test_when_finished rm clean-after-failure &&
		(exit 1)
	"
	test_expect_success "failure to clean up causes the test to fail" "
		test_when_finished \"(exit 2)\"
	"
	test_done
	EOF
	check_sub_test_lib_test failing-cleanup <<-\EOF
	> not ok 1 - tests clean up even after a failure
	> #	Z
	> #	touch clean-after-failure &&
	> #	test_when_finished rm clean-after-failure &&
	> #	(exit 1)
	> #	Z
	> not ok 2 - failure to clean up causes the test to fail
	> #	Z
	> #	test_when_finished "(exit 2)"
	> #	Z
	> # failed 2 among 2 test(s)
	> 1..2
	EOF
'

test_expect_success 'subtest: test_atexit is run' '
	write_and_run_sub_test_lib_test_err \
		atexit-cleanup -i <<-\EOF &&
	test_expect_success "tests clean up even after a failure" "
		> ../../clean-atexit &&
		test_atexit rm ../../clean-atexit &&
		> ../../also-clean-atexit &&
		test_atexit rm ../../also-clean-atexit &&
		> ../../dont-clean-atexit &&
		(exit 1)
	"
	test_done
	EOF
	test_path_is_file dont-clean-atexit &&
	test_path_is_missing clean-atexit &&
	test_path_is_missing also-clean-atexit
'

test_expect_success 'test_oid provides sane info by default' '
	test_oid zero >actual &&
	grep "^00*\$" actual &&
	rawsz="$(test_oid rawsz)" &&
	hexsz="$(test_oid hexsz)" &&
	# +1 accounts for the trailing newline
	test $(( $hexsz + 1)) -eq $(wc -c <actual) &&
	test $(( $rawsz * 2)) -eq "$hexsz"
'

test_expect_success 'test_oid can look up data for SHA-1' '
	test_when_finished "test_detect_hash" &&
	test_set_hash sha1 &&
	test_oid zero >actual &&
	grep "^00*\$" actual &&
	rawsz="$(test_oid rawsz)" &&
	hexsz="$(test_oid hexsz)" &&
	test $(wc -c <actual) -eq 41 &&
	test "$rawsz" -eq 20 &&
	test "$hexsz" -eq 40
'

test_expect_success 'test_oid can look up data for SHA-256' '
	test_when_finished "test_detect_hash" &&
	test_set_hash sha256 &&
	test_oid zero >actual &&
	grep "^00*\$" actual &&
	rawsz="$(test_oid rawsz)" &&
	hexsz="$(test_oid hexsz)" &&
	test $(wc -c <actual) -eq 65 &&
	test "$rawsz" -eq 32 &&
	test "$hexsz" -eq 64
'

test_expect_success 'test_oid can look up data for a specified algorithm' '
	rawsz="$(test_oid --hash=sha1 rawsz)" &&
	hexsz="$(test_oid --hash=sha1 hexsz)" &&
	test "$rawsz" -eq 20 &&
	test "$hexsz" -eq 40 &&
	rawsz="$(test_oid --hash=sha256 rawsz)" &&
	hexsz="$(test_oid --hash=sha256 hexsz)" &&
	test "$rawsz" -eq 32 &&
	test "$hexsz" -eq 64
'

test_expect_success 'test_bool_env' '
	(
		sane_unset envvar &&

		test_bool_env envvar true &&
		! test_bool_env envvar false &&

		envvar= &&
		export envvar &&
		! test_bool_env envvar true &&
		! test_bool_env envvar false &&

		envvar=true &&
		test_bool_env envvar true &&
		test_bool_env envvar false &&

		envvar=false &&
		! test_bool_env envvar true &&
		! test_bool_env envvar false &&

		envvar=invalid &&
		# When encountering an invalid bool value, test_bool_env
		# prints its error message to the original stderr of the
		# test script, hence the redirection of fd 7, and aborts
		# with "exit 1", hence the subshell.
		! ( test_bool_env envvar true ) 7>err &&
		grep "error: test_bool_env requires bool values" err &&

		envvar=true &&
		! ( test_bool_env envvar invalid ) 7>err &&
		grep "error: test_bool_env requires bool values" err
	)
'

################################################################
# Basics of the basics

test_oid_cache <<\EOF
path0f sha1:f87290f8eb2cbbea7857214459a0739927eab154
path0f sha256:638106af7c38be056f3212cbd7ac65bc1bac74f420ca5a436ff006a9d025d17d

path0s sha1:15a98433ae33114b085f3eb3bb03b832b3180a01
path0s sha256:3a24cc53cf68edddac490bbf94a418a52932130541361f685df685e41dd6c363

path2f sha1:3feff949ed00a62d9f7af97c15cd8a30595e7ac7
path2f sha256:2a7f36571c6fdbaf0e3f62751a0b25a3f4c54d2d1137b3f4af9cb794bb498e5f

path2s sha1:d8ce161addc5173867a3c3c730924388daedbc38
path2s sha256:18fd611b787c2e938ddcc248fabe4d66a150f9364763e9ec133dd01d5bb7c65a

path2d sha1:58a09c23e2ca152193f2786e06986b7b6712bdbe
path2d sha256:00e4b32b96e7e3d65d79112dcbea53238a22715f896933a62b811377e2650c17

path3f sha1:0aa34cae68d0878578ad119c86ca2b5ed5b28376
path3f sha256:09f58616b951bd571b8cb9dc76d372fbb09ab99db2393f5ab3189d26c45099ad

path3s sha1:8599103969b43aff7e430efea79ca4636466794f
path3s sha256:fce1aed087c053306f3f74c32c1a838c662bbc4551a7ac2420f5d6eb061374d0

path3d sha1:21ae8269cacbe57ae09138dcc3a2887f904d02b3
path3d sha256:9b60497be959cb830bf3f0dc82bcc9ad9e925a24e480837ade46b2295e47efe1

subp3f sha1:00fb5908cb97c2564a9783c0c64087333b3b464f
subp3f sha256:a1a9e16998c988453f18313d10375ee1d0ddefe757e710dcae0d66aa1e0c58b3

subp3s sha1:6649a1ebe9e9f1c553b66f5a6e74136a07ccc57c
subp3s sha256:81759d9f5e93c6546ecfcadb560c1ff057314b09f93fe8ec06e2d8610d34ef10

subp3d sha1:3c5e5399f3a333eddecce7a9b9465b63f65f51e2
subp3d sha256:76b4ef482d4fa1c754390344cf3851c7f883b27cf9bc999c6547928c46aeafb7

root sha1:087704a96baf1c2d1c869a8b084481e121c88b5b
root sha256:9481b52abab1b2ffeedbf9de63ce422b929f179c1b98ff7bee5f8f1bc0710751

simpletree sha1:7bb943559a305bdd6bdee2cef6e5df2413c3d30a
simpletree sha256:1710c07a6c86f9a3c7376364df04c47ee39e5a5e221fcdd84b743bc9bb7e2bc5
EOF

# updating a new file without --add should fail.
test_expect_success 'git update-index without --add should fail adding' '
	test_must_fail git update-index should-be-empty
'

# and with --add it should succeed, even if it is empty (it used to fail).
test_expect_success 'git update-index with --add should succeed' '
	git update-index --add should-be-empty
'

test_expect_success 'writing tree out with git write-tree' '
	tree=$(git write-tree)
'

# we know the shape and contents of the tree and know the object ID for it.
test_expect_success 'validate object ID of a known tree' '
	test "$tree" = "$(test_oid simpletree)"
    '

# Removing paths.
test_expect_success 'git update-index without --remove should fail removing' '
	rm -f should-be-empty full-of-directories &&
	test_must_fail git update-index should-be-empty
'

test_expect_success 'git update-index with --remove should be able to remove' '
	git update-index --remove should-be-empty
'

# Empty tree can be written with recent write-tree.
test_expect_success 'git write-tree should be able to write an empty tree' '
	tree=$(git write-tree)
'

test_expect_success 'validate object ID of a known tree' '
	test "$tree" = $EMPTY_TREE
'

# Various types of objects

test_expect_success 'adding various types of objects with git update-index --add' '
	mkdir path2 path3 path3/subp3 &&
	paths="path0 path2/file2 path3/file3 path3/subp3/file3" &&
	(
		for p in $paths
		do
			echo "hello $p" >$p || exit 1
			test_ln_s_add "hello $p" ${p}sym || exit 1
		done
	) &&
	find path* ! -type d -print | xargs git update-index --add
'

# Show them and see that matches what we expect.
test_expect_success 'showing stage with git ls-files --stage' '
	git ls-files --stage >current
'

test_expect_success 'validate git ls-files output for a known tree' '
	cat >expected <<-EOF &&
	100644 $(test_oid path0f) 0	path0
	120000 $(test_oid path0s) 0	path0sym
	100644 $(test_oid path2f) 0	path2/file2
	120000 $(test_oid path2s) 0	path2/file2sym
	100644 $(test_oid path3f) 0	path3/file3
	120000 $(test_oid path3s) 0	path3/file3sym
	100644 $(test_oid subp3f) 0	path3/subp3/file3
	120000 $(test_oid subp3s) 0	path3/subp3/file3sym
	EOF
	test_cmp expected current
'

test_expect_success 'writing tree out with git write-tree' '
	tree=$(git write-tree)
'

test_expect_success 'validate object ID for a known tree' '
	test "$tree" = "$(test_oid root)"
'

test_expect_success 'showing tree with git ls-tree' '
	git ls-tree $tree >current
'

test_expect_success 'git ls-tree output for a known tree' '
	cat >expected <<-EOF &&
	100644 blob $(test_oid path0f)	path0
	120000 blob $(test_oid path0s)	path0sym
	040000 tree $(test_oid path2d)	path2
	040000 tree $(test_oid path3d)	path3
	EOF
	test_cmp expected current
'

# This changed in ls-tree pathspec change -- recursive does
# not show tree nodes anymore.
test_expect_success 'showing tree with git ls-tree -r' '
	git ls-tree -r $tree >current
'

test_expect_success 'git ls-tree -r output for a known tree' '
	cat >expected <<-EOF &&
	100644 blob $(test_oid path0f)	path0
	120000 blob $(test_oid path0s)	path0sym
	100644 blob $(test_oid path2f)	path2/file2
	120000 blob $(test_oid path2s)	path2/file2sym
	100644 blob $(test_oid path3f)	path3/file3
	120000 blob $(test_oid path3s)	path3/file3sym
	100644 blob $(test_oid subp3f)	path3/subp3/file3
	120000 blob $(test_oid subp3s)	path3/subp3/file3sym
	EOF
	test_cmp expected current
'

# But with -r -t we can have both.
test_expect_success 'showing tree with git ls-tree -r -t' '
	git ls-tree -r -t $tree >current
'

test_expect_success 'git ls-tree -r output for a known tree' '
	cat >expected <<-EOF &&
	100644 blob $(test_oid path0f)	path0
	120000 blob $(test_oid path0s)	path0sym
	040000 tree $(test_oid path2d)	path2
	100644 blob $(test_oid path2f)	path2/file2
	120000 blob $(test_oid path2s)	path2/file2sym
	040000 tree $(test_oid path3d)	path3
	100644 blob $(test_oid path3f)	path3/file3
	120000 blob $(test_oid path3s)	path3/file3sym
	040000 tree $(test_oid subp3d)	path3/subp3
	100644 blob $(test_oid subp3f)	path3/subp3/file3
	120000 blob $(test_oid subp3s)	path3/subp3/file3sym
	EOF
	test_cmp expected current
'

test_expect_success 'writing partial tree out with git write-tree --prefix' '
	ptree=$(git write-tree --prefix=path3)
'

test_expect_success 'validate object ID for a known tree' '
	test "$ptree" = $(test_oid path3d)
'

test_expect_success 'writing partial tree out with git write-tree --prefix' '
	ptree=$(git write-tree --prefix=path3/subp3)
'

test_expect_success 'validate object ID for a known tree' '
	test "$ptree" = $(test_oid subp3d)
'

test_expect_success 'put invalid objects into the index' '
	rm -f .git/index &&
	suffix=$(echo $ZERO_OID | sed -e "s/^.//") &&
	cat >badobjects <<-EOF &&
	100644 blob $(test_oid 001)	dir/file1
	100644 blob $(test_oid 002)	dir/file2
	100644 blob $(test_oid 003)	dir/file3
	100644 blob $(test_oid 004)	dir/file4
	100644 blob $(test_oid 005)	dir/file5
	EOF
	git update-index --index-info <badobjects
'

test_expect_success 'writing this tree without --missing-ok' '
	test_must_fail git write-tree
'

test_expect_success 'writing this tree with --missing-ok' '
	git write-tree --missing-ok
'


################################################################
test_expect_success 'git read-tree followed by write-tree should be idempotent' '
	rm -f .git/index &&
	git read-tree $tree &&
	test_path_is_file .git/index &&
	newtree=$(git write-tree) &&
	test "$newtree" = "$tree"
'

test_expect_success 'validate git diff-files output for a know cache/work tree state' '
	cat >expected <<EOF &&
:100644 100644 $(test_oid path0f) $ZERO_OID M	path0
:120000 120000 $(test_oid path0s) $ZERO_OID M	path0sym
:100644 100644 $(test_oid path2f) $ZERO_OID M	path2/file2
:120000 120000 $(test_oid path2s) $ZERO_OID M	path2/file2sym
:100644 100644 $(test_oid path3f) $ZERO_OID M	path3/file3
:120000 120000 $(test_oid path3s) $ZERO_OID M	path3/file3sym
:100644 100644 $(test_oid subp3f) $ZERO_OID M	path3/subp3/file3
:120000 120000 $(test_oid subp3s) $ZERO_OID M	path3/subp3/file3sym
EOF
	git diff-files >current &&
	test_cmp expected current
'

test_expect_success 'git update-index --refresh should succeed' '
	git update-index --refresh
'

test_expect_success 'no diff after checkout and git update-index --refresh' '
	git diff-files >current &&
	cmp -s current /dev/null
'

################################################################
P=$(test_oid root)

test_expect_success 'git commit-tree records the correct tree in a commit' '
	commit0=$(echo NO | git commit-tree $P) &&
	git show --pretty=raw $commit0 >out &&
	tree=$(sed -n -e "s/^tree //p" -e "/^author /q" out) &&
	test "z$tree" = "z$P"
'

test_expect_success 'git commit-tree records the correct parent in a commit' '
	commit1=$(echo NO | git commit-tree $P -p $commit0) &&
	git show --pretty=raw $commit1 >out &&
	parent=$(sed -n -e "s/^parent //p" -e "/^author /q" out) &&
	test "z$commit0" = "z$parent"
'

test_expect_success 'git commit-tree omits duplicated parent in a commit' '
	commit2=$(echo NO | git commit-tree $P -p $commit0 -p $commit0) &&
	git show --pretty=raw $commit2 >out &&
	cat >match.sed <<-\EOF &&
	s/^parent //p
	/^author /q
	EOF
	parent=$(sed -n -f match.sed out | sort -u) &&
	test "z$commit0" = "z$parent" &&
	git show --pretty=raw $commit2 >out &&
	test_stdout_line_count = 1 sed -n -f match.sed out
'

test_expect_success 'update-index D/F conflict' '
	mv path0 tmp &&
	mv path2 path0 &&
	mv tmp path2 &&
	git update-index --add --replace path2 path0/file2 &&
	git ls-files path0 >tmp &&
	numpath0=$(wc -l <tmp) &&
	test $numpath0 = 1
'

test_expect_success 'very long name in the index handled sanely' '

	a=a && # 1
	a=$a$a$a$a$a$a$a$a$a$a$a$a$a$a$a$a && # 16
	a=$a$a$a$a$a$a$a$a$a$a$a$a$a$a$a$a && # 256
	a=$a$a$a$a$a$a$a$a$a$a$a$a$a$a$a$a && # 4096
	a=${a}q &&

	>path4 &&
	git update-index --add path4 &&
	git ls-files -s path4 >tmp &&
	(
		sed -e "s/	.*/	/" tmp |
		tr -d "\012" &&
		echo "$a"
	) | git update-index --index-info &&
	git ls-files "a*" >tmp &&
	len=$(wc -c <tmp) &&
	test $len = 4098
'

# D/F conflict checking uses an optimization when adding to the end.
# make sure it does not get confused by `a-` sorting _between_
# `a` and `a/`.
test_expect_success 'more update-index D/F conflicts' '
	# empty the index to make sure our entry is last
	git read-tree --empty &&
	cacheinfo=100644,$(test_oid empty_blob) &&
	git update-index --add --cacheinfo $cacheinfo,path5/a &&

	test_must_fail git update-index --add --cacheinfo $cacheinfo,path5/a/file &&
	test_must_fail git update-index --add --cacheinfo $cacheinfo,path5/a/b/file &&
	test_must_fail git update-index --add --cacheinfo $cacheinfo,path5/a/b/c/file &&

	# "a-" sorts between "a" and "a/"
	git update-index --add --cacheinfo $cacheinfo,path5/a- &&

	test_must_fail git update-index --add --cacheinfo $cacheinfo,path5/a/file &&
	test_must_fail git update-index --add --cacheinfo $cacheinfo,path5/a/b/file &&
	test_must_fail git update-index --add --cacheinfo $cacheinfo,path5/a/b/c/file &&

	cat >expected <<-\EOF &&
	path5/a
	path5/a-
	EOF
	git ls-files >actual &&
	test_cmp expected actual
'

test_expect_success 'test_must_fail on a failing git command' '
	test_must_fail git notacommand
'

test_expect_success 'test_must_fail on a failing git command with env' '
	test_must_fail env var1=a var2=b git notacommand
'

test_expect_success 'test_must_fail rejects a non-git command' '
	! test_must_fail grep ^$ notafile 2>err &&
	grep -F "test_must_fail: only '"'"'git'"'"' is allowed" err
'

test_expect_success 'test_must_fail rejects a non-git command with env' '
	! test_must_fail env var1=a var2=b grep ^$ notafile 2>err &&
	grep -F "test_must_fail: only '"'"'git'"'"' is allowed" err
'

test_done
