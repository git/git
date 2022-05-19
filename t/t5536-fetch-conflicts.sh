#!/bin/sh

test_description='fetch handles conflicting refspecs correctly'

. ./test-lib.sh

D=$(pwd)

setup_repository () {
	but init "$1" && (
		cd "$1" &&
		but config remote.origin.url "$D" &&
		shift &&
		for refspec in "$@"
		do
			but config --add remote.origin.fetch "$refspec"
		done
	)
}

test_expect_success 'setup' '
	but cummit --allow-empty -m "Initial" &&
	but branch branch1 &&
	but tag tag1 &&
	but cummit --allow-empty -m "First" &&
	but branch branch2 &&
	but tag tag2
'

test_expect_success 'fetch with no conflict' '
	setup_repository ok "+refs/heads/*:refs/remotes/origin/*" && (
		cd ok &&
		but fetch origin
	)
'

test_expect_success 'fetch conflict: config vs. config' '
	setup_repository ccc \
		"+refs/heads/branch1:refs/remotes/origin/branch1" \
		"+refs/heads/branch2:refs/remotes/origin/branch1" && (
		cd ccc &&
		test_must_fail but fetch origin 2>error &&
		test_i18ngrep "fatal: Cannot fetch both refs/heads/branch1 and refs/heads/branch2 to refs/remotes/origin/branch1" error
	)
'

test_expect_success 'fetch duplicate: config vs. config' '
	setup_repository dcc \
		"+refs/heads/*:refs/remotes/origin/*" \
		"+refs/heads/branch1:refs/remotes/origin/branch1" && (
		cd dcc &&
		but fetch origin
	)
'

test_expect_success 'fetch conflict: arg overrides config' '
	setup_repository aoc \
		"+refs/heads/*:refs/remotes/origin/*" && (
		cd aoc &&
		but fetch origin refs/heads/branch2:refs/remotes/origin/branch1
	)
'

test_expect_success 'fetch conflict: arg vs. arg' '
	setup_repository caa && (
		cd caa &&
		test_must_fail but fetch origin \
			refs/heads/*:refs/remotes/origin/* \
			refs/heads/branch2:refs/remotes/origin/branch1 2>error &&
		test_i18ngrep "fatal: Cannot fetch both refs/heads/branch1 and refs/heads/branch2 to refs/remotes/origin/branch1" error
	)
'

test_expect_success 'fetch conflict: criss-cross args' '
	setup_repository xaa \
		"+refs/heads/*:refs/remotes/origin/*" && (
		cd xaa &&
		but fetch origin \
			refs/heads/branch1:refs/remotes/origin/branch2 \
			refs/heads/branch2:refs/remotes/origin/branch1 2>error &&
		test_i18ngrep "warning: refs/remotes/origin/branch1 usually tracks refs/heads/branch1, not refs/heads/branch2" error &&
		test_i18ngrep "warning: refs/remotes/origin/branch2 usually tracks refs/heads/branch2, not refs/heads/branch1" error
	)
'

test_done
