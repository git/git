#!/bin/sh

test_description='pushing to a mirror repository'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
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
		git init &&
		git config receive.denyCurrentBranch warn
	) &&
	mkdir main &&
	(
		cd main &&
		git init &&
		git remote add $1 up ../mirror
	)
}


# BRANCH tests
test_expect_success 'push mirror creates new branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/heads/main) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/heads/main) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror updates existing branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git push --mirror up &&
		echo two >foo && git add foo && git commit -m two &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/heads/main) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/heads/main) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror force updates existing branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git push --mirror up &&
		echo two >foo && git add foo && git commit -m two &&
		git push --mirror up &&
		git reset --hard HEAD^ &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/heads/main) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/heads/main) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror removes branches' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git branch remove main &&
		git push --mirror up &&
		git branch -D remove &&
		git push --mirror up
	) &&
	(
		cd mirror &&
		invert git show-ref -s --verify refs/heads/remove
	)

'

test_expect_success 'push mirror adds, updates and removes branches together' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git branch remove main &&
		git push --mirror up &&
		git branch -D remove &&
		git branch add main &&
		echo two >foo && git add foo && git commit -m two &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/heads/main) &&
	main_add=$(cd main && git show-ref -s --verify refs/heads/add) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/heads/main) &&
	mirror_add=$(cd mirror && git show-ref -s --verify refs/heads/add) &&
	test "$main_main" = "$mirror_main" &&
	test "$main_add" = "$mirror_add" &&
	(
		cd mirror &&
		invert git show-ref -s --verify refs/heads/remove
	)

'


# TAG tests
test_expect_success 'push mirror creates new tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git tag -f tmain main &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/tags/tmain) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/tags/tmain) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror updates existing tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git tag -f tmain main &&
		git push --mirror up &&
		echo two >foo && git add foo && git commit -m two &&
		git tag -f tmain main &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/tags/tmain) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/tags/tmain) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror force updates existing tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git tag -f tmain main &&
		git push --mirror up &&
		echo two >foo && git add foo && git commit -m two &&
		git tag -f tmain main &&
		git push --mirror up &&
		git reset --hard HEAD^ &&
		git tag -f tmain main &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/tags/tmain) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/tags/tmain) &&
	test "$main_main" = "$mirror_main"

'

test_expect_success 'push mirror removes tags' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git tag -f tremove main &&
		git push --mirror up &&
		git tag -d tremove &&
		git push --mirror up
	) &&
	(
		cd mirror &&
		invert git show-ref -s --verify refs/tags/tremove
	)

'

test_expect_success 'push mirror adds, updates and removes tags together' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git tag -f tmain main &&
		git tag -f tremove main &&
		git push --mirror up &&
		git tag -d tremove &&
		git tag tadd main &&
		echo two >foo && git add foo && git commit -m two &&
		git tag -f tmain main &&
		git push --mirror up
	) &&
	main_main=$(cd main && git show-ref -s --verify refs/tags/tmain) &&
	main_add=$(cd main && git show-ref -s --verify refs/tags/tadd) &&
	mirror_main=$(cd mirror && git show-ref -s --verify refs/tags/tmain) &&
	mirror_add=$(cd mirror && git show-ref -s --verify refs/tags/tadd) &&
	test "$main_main" = "$mirror_main" &&
	test "$main_add" = "$mirror_add" &&
	(
		cd mirror &&
		invert git show-ref -s --verify refs/tags/tremove
	)

'

test_expect_success 'remote.foo.mirror adds and removes branches' '

	mk_repo_pair --mirror &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git branch keep main &&
		git branch remove main &&
		git push up &&
		git branch -D remove &&
		git push up
	) &&
	(
		cd mirror &&
		git show-ref -s --verify refs/heads/keep &&
		invert git show-ref -s --verify refs/heads/remove
	)

'

test_expect_success 'remote.foo.mirror=no has no effect' '

	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git config --add remote.up.mirror no &&
		git branch keep main &&
		git push --mirror up &&
		git branch -D keep &&
		git push up :
	) &&
	(
		cd mirror &&
		git show-ref -s --verify refs/heads/keep
	)

'

test_expect_success 'push to mirrored repository with refspec fails' '
	mk_repo_pair &&
	(
		cd main &&
		echo one >foo && git add foo && git commit -m one &&
		git config --add remote.up.mirror true &&
		test_must_fail git push up main
	)
'

test_done
