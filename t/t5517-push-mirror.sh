#!/bin/sh

test_description='pushing to a mirror repository'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

D=$(pwd)

invert () {
	if "$@"; then
		return 1
	else
		return 0
	fi
}

mk_repo_pair () {
	rm -rf main mirror &&
	mkdir mirror &&
	(
		cd mirror &&
		but init &&
		but config receive.denyCurrentBranch warn
	) &&
	mkdir main &&
	(
		cd main &&
		but init &&
		but remote add $1 up ../mirror
	)
}


# BRANCH tests
test_expect_success 'push mirror creates new branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/heads/main) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/heads/main) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror updates existing branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but push --mirror up &&
		echo two >foo && but add foo && but cummit -m two &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/heads/main) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/heads/main) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror force updates existing branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but push --mirror up &&
		echo two >foo && but add foo && but cummit -m two &&
		but push --mirror up &&
		but reset --hard HEAD^ &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/heads/main) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/heads/main) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror removes branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but branch remove main &&
		but push --mirror up &&
		but branch -D remove &&
		but push --mirror up
	) &&
	(
		cd mirror &&
		invert but show-ref -s --verify refs/heads/remove
	)

'

test_expect_success 'push mirror adds, updates and removes branches together' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but branch remove main &&
		but push --mirror up &&
		but branch -D remove &&
		but branch add main &&
		echo two >foo && but add foo && but cummit -m two &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/heads/main) &&
	main_add=$(cd main && but show-ref -s --verify refs/heads/add) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/heads/main) &&
	mirror_add=$(cd mirror && but show-ref -s --verify refs/heads/add) &&
	test "$main_main" = "$mirror_main" &&
	test "$main_add" = "$mirror_add" &&
	(
		cd mirror &&
		invert but show-ref -s --verify refs/heads/remove
	)

'


# TAG tests
test_expect_success 'push mirror creates new tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but tag -f tmain main &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/tags/tmain) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/tags/tmain) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror updates existing tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but tag -f tmain main &&
		but push --mirror up &&
		echo two >foo && but add foo && but cummit -m two &&
		but tag -f tmain main &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/tags/tmain) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/tags/tmain) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror force updates existing tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but tag -f tmain main &&
		but push --mirror up &&
		echo two >foo && but add foo && but cummit -m two &&
		but tag -f tmain main &&
		but push --mirror up &&
		but reset --hard HEAD^ &&
		but tag -f tmain main &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/tags/tmain) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/tags/tmain) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror removes tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but tag -f tremove main &&
		but push --mirror up &&
		but tag -d tremove &&
		but push --mirror up
	) &&
	(
		cd mirror &&
		invert but show-ref -s --verify refs/tags/tremove
	)

'

test_expect_success 'push mirror adds, updates and removes tags together' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but tag -f tmain main &&
		but tag -f tremove main &&
		but push --mirror up &&
		but tag -d tremove &&
		but tag tadd main &&
		echo two >foo && but add foo && but cummit -m two &&
		but tag -f tmain main &&
		but push --mirror up
	) &&
	main_main=$(cd main && but show-ref -s --verify refs/tags/tmain) &&
	main_add=$(cd main && but show-ref -s --verify refs/tags/tadd) &&
	mirror_main=$(cd mirror && but show-ref -s --verify refs/tags/tmain) &&
	mirror_add=$(cd mirror && but show-ref -s --verify refs/tags/tadd) &&
	test "$main_main" = "$mirror_main" &&
	test "$main_add" = "$mirror_add" &&
	(
		cd mirror &&
		invert but show-ref -s --verify refs/tags/tremove
	)

'

test_expect_success 'remote.foo.mirror adds and removes branches' '

	mk_repo_pair --mirror &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but branch keep main &&
		but branch remove main &&
		but push up &&
		but branch -D remove &&
		but push up
	) &&
	(
		cd mirror &&
		but show-ref -s --verify refs/heads/keep &&
		invert but show-ref -s --verify refs/heads/remove
	)

'

test_expect_success 'remote.foo.mirror=no has no effect' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but config --add remote.up.mirror no &&
		but branch keep main &&
		but push --mirror up &&
		but branch -D keep &&
		but push up :
	) &&
	(
		cd mirror &&
		but show-ref -s --verify refs/heads/keep
	)

'

test_expect_success 'push to mirrored repository with refspec fails' '
	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && but add foo && but cummit -m one &&
		but config --add remote.up.mirror true &&
		test_must_fail but push up main
	)
'

test_done
