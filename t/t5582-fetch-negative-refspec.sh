#!/bin/sh
# Copyright (c) 2020, Jacob Keller.

test_description='"git fetch" with negative refspecs.

'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo >file original &&
	git add file &&
	git commit -a -m original
'

test_expect_success "clone and setup child repos" '
	git clone . one &&
	(
		cd one &&
		echo >file updated by one &&
		git commit -a -m "updated by one" &&
		git switch -c alternate &&
		echo >file updated again by one &&
		git commit -a -m "updated by one again" &&
		git switch main
	) &&
	git clone . two &&
	(
		cd two &&
		git config branch.main.remote one &&
		git config remote.one.url ../one/.git/ &&
		git config remote.one.fetch +refs/heads/*:refs/remotes/one/* &&
		git config --add remote.one.fetch ^refs/heads/alternate
	) &&
	git clone . three
'

test_expect_success "fetch one" '
	echo >file updated by origin &&
	git commit -a -m "updated by origin" &&
	(
		cd two &&
		test_must_fail git rev-parse --verify refs/remotes/one/alternate &&
		git fetch one &&
		test_must_fail git rev-parse --verify refs/remotes/one/alternate &&
		git rev-parse --verify refs/remotes/one/main &&
		mine=$(git rev-parse refs/remotes/one/main) &&
		his=$(cd ../one && git rev-parse refs/heads/main) &&
		test "z$mine" = "z$his"
	)
'

test_expect_success "fetch with negative refspec on commandline" '
	echo >file updated by origin again &&
	git commit -a -m "updated by origin again" &&
	(
		cd three &&
		alternate_in_one=$(cd ../one && git rev-parse refs/heads/alternate) &&
		echo $alternate_in_one >expect &&
		git fetch ../one/.git refs/heads/*:refs/remotes/one/* ^refs/heads/main &&
		cut -f -1 .git/FETCH_HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch with negative sha1 refspec fails" '
	echo >file updated by origin yet again &&
	git commit -a -m "updated by origin yet again" &&
	(
		cd three &&
		main_in_one=$(cd ../one && git rev-parse refs/heads/main) &&
		test_must_fail git fetch ../one/.git refs/heads/*:refs/remotes/one/* ^$main_in_one
	)
'

test_expect_success "fetch with negative pattern refspec" '
	echo >file updated by origin once more &&
	git commit -a -m "updated by origin once more" &&
	(
		cd three &&
		alternate_in_one=$(cd ../one && git rev-parse refs/heads/alternate) &&
		echo $alternate_in_one >expect &&
		git fetch ../one/.git refs/heads/*:refs/remotes/one/* ^refs/heads/m* &&
		cut -f -1 .git/FETCH_HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch with negative pattern refspec does not expand prefix" '
	echo >file updated by origin another time &&
	git commit -a -m "updated by origin another time" &&
	(
		cd three &&
		alternate_in_one=$(cd ../one && git rev-parse refs/heads/alternate) &&
		main_in_one=$(cd ../one && git rev-parse refs/heads/main) &&
		echo $alternate_in_one >expect &&
		echo $main_in_one >>expect &&
		git fetch ../one/.git refs/heads/*:refs/remotes/one/* ^main &&
		cut -f -1 .git/FETCH_HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch with negative refspec avoids duplicate conflict" '
	cd "$D" &&
	(
		cd one &&
		git branch dups/a &&
		git branch dups/b &&
		git branch dups/c &&
		git branch other/a &&
		git rev-parse --verify refs/heads/other/a >../expect &&
		git rev-parse --verify refs/heads/dups/b >>../expect &&
		git rev-parse --verify refs/heads/dups/c >>../expect
	) &&
	(
		cd three &&
		git fetch ../one/.git ^refs/heads/dups/a refs/heads/dups/*:refs/dups/* refs/heads/other/a:refs/dups/a &&
		git rev-parse --verify refs/dups/a >../actual &&
		git rev-parse --verify refs/dups/b >>../actual &&
		git rev-parse --verify refs/dups/c >>../actual
	) &&
	test_cmp expect actual
'

test_expect_success "push --prune with negative refspec" '
	(
		cd two &&
		git branch prune/a &&
		git branch prune/b &&
		git branch prune/c &&
		git push ../three refs/heads/prune/* &&
		git branch -d prune/a &&
		git branch -d prune/b &&
		git push --prune ../three refs/heads/prune/* ^refs/heads/prune/b
	) &&
	(
		cd three &&
		test_write_lines b c >expect &&
		git for-each-ref --format="%(refname:lstrip=3)" refs/heads/prune/ >actual &&
		test_cmp expect actual
	)
'

test_expect_success "push --prune with negative refspec apply to the destination" '
	(
		cd two &&
		git branch ours/a &&
		git branch ours/b &&
		git branch ours/c &&
		git push ../three refs/heads/ours/*:refs/heads/theirs/* &&
		git branch -d ours/a &&
		git branch -d ours/b &&
		git push --prune ../three refs/heads/ours/*:refs/heads/theirs/* ^refs/heads/theirs/b
	) &&
	(
		cd three &&
		test_write_lines b c >expect &&
		git for-each-ref --format="%(refname:lstrip=3)" refs/heads/theirs/ >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch --prune with negative refspec" '
	(
		cd two &&
		git branch fetch/a &&
		git branch fetch/b &&
		git branch fetch/c
	) &&
	(
		cd three &&
		git fetch ../two/.git refs/heads/fetch/*:refs/heads/copied/*
	) &&
	(
		cd two &&
		git branch -d fetch/a &&
		git branch -d fetch/b
	) &&
	(
		cd three &&
		test_write_lines b c >expect &&
		git fetch -v ../two/.git --prune refs/heads/fetch/*:refs/heads/copied/* ^refs/heads/fetch/b &&
		git for-each-ref --format="%(refname:lstrip=3)" refs/heads/copied/ >actual &&
		test_cmp expect actual
	)
'

test_done
