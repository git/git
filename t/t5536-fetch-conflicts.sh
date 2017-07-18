#!/bin/sh

test_description='fetch handles conflicting refspecs correctly'

. ./test-lib.sh

D=$(pwd)

setup_repository () {
	git init "$1" && (
		cd "$1" &&
		git config remote.origin.url "$D" &&
		shift &&
		for refspec in "$@"
		do
			git config --add remote.origin.fetch "$refspec"
		done
	)
}

verify_stderr () {
	cat >expected &&
	# We're not interested in the error
	# "fatal: The remote end hung up unexpectedly":
	grep -E '^(fatal|warning):' <error | grep -v 'hung up' >actual | sort &&
	test_cmp expected actual
}

test_expect_success 'setup' '
	git commit --allow-empty -m "Initial" &&
	git branch branch1 &&
	git tag tag1 &&
	git commit --allow-empty -m "First" &&
	git branch branch2 &&
	git tag tag2
'

test_expect_success 'fetch with no conflict' '
	setup_repository ok "+refs/heads/*:refs/remotes/origin/*" && (
		cd ok &&
		git fetch origin
	)
'

test_expect_success 'fetch conflict: config vs. config' '
	setup_repository ccc \
		"+refs/heads/branch1:refs/remotes/origin/branch1" \
		"+refs/heads/branch2:refs/remotes/origin/branch1" && (
		cd ccc &&
		test_must_fail git fetch origin 2>error &&
		verify_stderr <<-\EOF
		fatal: Cannot fetch both refs/heads/branch1 and refs/heads/branch2 to refs/remotes/origin/branch1
		EOF
	)
'

test_expect_success 'fetch duplicate: config vs. config' '
	setup_repository dcc \
		"+refs/heads/*:refs/remotes/origin/*" \
		"+refs/heads/branch1:refs/remotes/origin/branch1" && (
		cd dcc &&
		git fetch origin
	)
'

test_expect_success 'fetch conflict: arg overrides config' '
	setup_repository aoc \
		"+refs/heads/*:refs/remotes/origin/*" && (
		cd aoc &&
		git fetch origin refs/heads/branch2:refs/remotes/origin/branch1
	)
'

test_expect_success 'fetch conflict: arg vs. arg' '
	setup_repository caa && (
		cd caa &&
		test_must_fail git fetch origin \
			refs/heads/*:refs/remotes/origin/* \
			refs/heads/branch2:refs/remotes/origin/branch1 2>error &&
		verify_stderr <<-\EOF
		fatal: Cannot fetch both refs/heads/branch1 and refs/heads/branch2 to refs/remotes/origin/branch1
		EOF
	)
'

test_expect_success 'fetch conflict: criss-cross args' '
	setup_repository xaa \
		"+refs/heads/*:refs/remotes/origin/*" && (
		cd xaa &&
		git fetch origin \
			refs/heads/branch1:refs/remotes/origin/branch2 \
			refs/heads/branch2:refs/remotes/origin/branch1 2>error &&
		verify_stderr <<-\EOF
		warning: refs/remotes/origin/branch1 usually tracks refs/heads/branch1, not refs/heads/branch2
		warning: refs/remotes/origin/branch2 usually tracks refs/heads/branch2, not refs/heads/branch1
		EOF
	)
'

test_done
