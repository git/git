#!/bin/sh

test_description='push with --set-upstream'
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

test_expect_success 'push -u master:master' '
	git push -u upstream master:master &&
	check_config master upstream refs/heads/master
'

test_expect_success 'push -u master:other' '
	git push -u upstream master:other &&
	check_config master upstream refs/heads/other
'

test_expect_success 'push -u --dry-run master:otherX' '
	git push -u --dry-run upstream master:otherX &&
	check_config master upstream refs/heads/other
'

test_expect_success 'push -u master2:master2' '
	git branch master2 &&
	git push -u upstream master2:master2 &&
	check_config master2 upstream refs/heads/master2
'

test_expect_success 'push -u master2:other2' '
	git push -u upstream master2:other2 &&
	check_config master2 upstream refs/heads/other2
'

test_expect_success 'push -u :master2' '
	git push -u upstream :master2 &&
	check_config master2 upstream refs/heads/other2
'

test_expect_success 'push -u --all' '
	git branch all1 &&
	git branch all2 &&
	git push -u --all &&
	check_config all1 upstream refs/heads/all1 &&
	check_config all2 upstream refs/heads/all2
'

test_expect_success 'push -u HEAD' '
	git checkout -b headbranch &&
	git push -u upstream HEAD &&
	check_config headbranch upstream refs/heads/headbranch
'

test_expect_success TTY 'progress messages go to tty' '
	ensure_fresh_upstream &&

	test_terminal git push -u upstream master >out 2>err &&
	test_i18ngrep "Writing objects" err
'

test_expect_success 'progress messages do not go to non-tty' '
	ensure_fresh_upstream &&

	# skip progress messages, since stderr is non-tty
	git push -u upstream master >out 2>err &&
	test_i18ngrep ! "Writing objects" err
'

test_expect_success 'progress messages go to non-tty (forced)' '
	ensure_fresh_upstream &&

	# force progress messages to stderr, even though it is non-tty
	git push -u --progress upstream master >out 2>err &&
	test_i18ngrep "Writing objects" err
'

test_expect_success TTY 'push -q suppresses progress' '
	ensure_fresh_upstream &&

	test_terminal git push -u -q upstream master >out 2>err &&
	test_i18ngrep ! "Writing objects" err
'

test_expect_success TTY 'push --no-progress suppresses progress' '
	ensure_fresh_upstream &&

	test_terminal git push -u --no-progress upstream master >out 2>err &&
	test_i18ngrep ! "Unpacking objects" err &&
	test_i18ngrep ! "Writing objects" err
'

test_expect_success TTY 'quiet push' '
	ensure_fresh_upstream &&

	test_terminal git push --quiet --no-progress upstream master 2>&1 | tee output &&
	test_must_be_empty output
'

test_done
