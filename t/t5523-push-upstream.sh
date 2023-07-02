#!/bin/sh

test_description='push with --set-upstream'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

ensure_fresh_upstream() {
	rm -rf parent && git init --bare parent
}

test_expect_success 'setup bare parent' '
	ensure_fresh_upstream &&
	git remote add upstream parent
'

test_expect_success 'setup local commit' '
	echo content >file &&
	git add file &&
	git commit -m one
'

check_config() {
	(echo $2; echo $3) >expect.$1
	(git config branch.$1.remote
	 git config branch.$1.merge) >actual.$1
	test_cmp expect.$1 actual.$1
}

test_expect_success 'push -u main:main' '
	git push -u upstream main:main &&
	check_config main upstream refs/heads/main
'

test_expect_success 'push -u main:other' '
	git push -u upstream main:other &&
	check_config main upstream refs/heads/other
'

test_expect_success 'push -u --dry-run main:otherX' '
	git push -u --dry-run upstream main:otherX &&
	check_config main upstream refs/heads/other
'

test_expect_success 'push -u topic_2:topic_2' '
	git branch topic_2 &&
	git push -u upstream topic_2:topic_2 &&
	check_config topic_2 upstream refs/heads/topic_2
'

test_expect_success 'push -u topic_2:other2' '
	git push -u upstream topic_2:other2 &&
	check_config topic_2 upstream refs/heads/other2
'

test_expect_success 'push -u :topic_2' '
	git push -u upstream :topic_2 &&
	check_config topic_2 upstream refs/heads/other2
'

test_expect_success 'push -u --all(the same behavior with--branches)' '
	git branch all1 &&
	git branch all2 &&
	git push -u --all &&
	check_config all1 upstream refs/heads/all1 &&
	check_config all2 upstream refs/heads/all2 &&
	git config --get-regexp branch.all* > expect &&
	git config --remove-section branch.all1 &&
	git config --remove-section branch.all2 &&
	git push -u --branches &&
	check_config all1 upstream refs/heads/all1 &&
	check_config all2 upstream refs/heads/all2 &&
	git config --get-regexp branch.all* > actual &&
	test_cmp expect actual
'

test_expect_success 'push -u HEAD' '
	git checkout -b headbranch &&
	git push -u upstream HEAD &&
	check_config headbranch upstream refs/heads/headbranch
'

test_expect_success TTY 'progress messages go to tty' '
	ensure_fresh_upstream &&

	test_terminal git push -u upstream main >out 2>err &&
	test_i18ngrep "Writing objects" err
'

test_expect_success 'progress messages do not go to non-tty' '
	ensure_fresh_upstream &&

	# skip progress messages, since stderr is non-tty
	git push -u upstream main >out 2>err &&
	test_i18ngrep ! "Writing objects" err
'

test_expect_success 'progress messages go to non-tty (forced)' '
	ensure_fresh_upstream &&

	# force progress messages to stderr, even though it is non-tty
	git push -u --progress upstream main >out 2>err &&
	test_i18ngrep "Writing objects" err
'

test_expect_success TTY 'push -q suppresses progress' '
	ensure_fresh_upstream &&

	test_terminal git push -u -q upstream main >out 2>err &&
	test_i18ngrep ! "Writing objects" err
'

test_expect_success TTY 'push --no-progress suppresses progress' '
	ensure_fresh_upstream &&

	test_terminal git push -u --no-progress upstream main >out 2>err &&
	test_i18ngrep ! "Unpacking objects" err &&
	test_i18ngrep ! "Writing objects" err
'

test_expect_success TTY 'quiet push' '
	ensure_fresh_upstream &&

	test_terminal git push --quiet --no-progress upstream main 2>&1 | tee output &&
	test_must_be_empty output
'

test_expect_success TTY 'quiet push -u' '
	ensure_fresh_upstream &&

	test_terminal git push --quiet -u --no-progress upstream main 2>&1 | tee output &&
	test_must_be_empty output
'

test_done
