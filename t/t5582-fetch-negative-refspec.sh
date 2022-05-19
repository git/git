#!/bin/sh
# Copyright (c) 2020, Jacob Keller.

test_description='"but fetch" with negative refspecs.

'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo >file original &&
	but add file &&
	but cummit -a -m original
'

test_expect_success "clone and setup child repos" '
	but clone . one &&
	(
		cd one &&
		echo >file updated by one &&
		but cummit -a -m "updated by one" &&
		but switch -c alternate &&
		echo >file updated again by one &&
		but cummit -a -m "updated by one again" &&
		but switch main
	) &&
	but clone . two &&
	(
		cd two &&
		but config branch.main.remote one &&
		but config remote.one.url ../one/.but/ &&
		but config remote.one.fetch +refs/heads/*:refs/remotes/one/* &&
		but config --add remote.one.fetch ^refs/heads/alternate
	) &&
	but clone . three
'

test_expect_success "fetch one" '
	echo >file updated by origin &&
	but cummit -a -m "updated by origin" &&
	(
		cd two &&
		test_must_fail but rev-parse --verify refs/remotes/one/alternate &&
		but fetch one &&
		test_must_fail but rev-parse --verify refs/remotes/one/alternate &&
		but rev-parse --verify refs/remotes/one/main &&
		mine=$(but rev-parse refs/remotes/one/main) &&
		his=$(cd ../one && but rev-parse refs/heads/main) &&
		test "z$mine" = "z$his"
	)
'

test_expect_success "fetch with negative refspec on commandline" '
	echo >file updated by origin again &&
	but cummit -a -m "updated by origin again" &&
	(
		cd three &&
		alternate_in_one=$(cd ../one && but rev-parse refs/heads/alternate) &&
		echo $alternate_in_one >expect &&
		but fetch ../one/.but refs/heads/*:refs/remotes/one/* ^refs/heads/main &&
		cut -f -1 .but/FETCH_HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch with negative sha1 refspec fails" '
	echo >file updated by origin yet again &&
	but cummit -a -m "updated by origin yet again" &&
	(
		cd three &&
		main_in_one=$(cd ../one && but rev-parse refs/heads/main) &&
		test_must_fail but fetch ../one/.but refs/heads/*:refs/remotes/one/* ^$main_in_one
	)
'

test_expect_success "fetch with negative pattern refspec" '
	echo >file updated by origin once more &&
	but cummit -a -m "updated by origin once more" &&
	(
		cd three &&
		alternate_in_one=$(cd ../one && but rev-parse refs/heads/alternate) &&
		echo $alternate_in_one >expect &&
		but fetch ../one/.but refs/heads/*:refs/remotes/one/* ^refs/heads/m* &&
		cut -f -1 .but/FETCH_HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch with negative pattern refspec does not expand prefix" '
	echo >file updated by origin another time &&
	but cummit -a -m "updated by origin another time" &&
	(
		cd three &&
		alternate_in_one=$(cd ../one && but rev-parse refs/heads/alternate) &&
		main_in_one=$(cd ../one && but rev-parse refs/heads/main) &&
		echo $alternate_in_one >expect &&
		echo $main_in_one >>expect &&
		but fetch ../one/.but refs/heads/*:refs/remotes/one/* ^main &&
		cut -f -1 .but/FETCH_HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch with negative refspec avoids duplicate conflict" '
	(
		cd one &&
		but branch dups/a &&
		but branch dups/b &&
		but branch dups/c &&
		but branch other/a &&
		but rev-parse --verify refs/heads/other/a >../expect &&
		but rev-parse --verify refs/heads/dups/b >>../expect &&
		but rev-parse --verify refs/heads/dups/c >>../expect
	) &&
	(
		cd three &&
		but fetch ../one/.but ^refs/heads/dups/a refs/heads/dups/*:refs/dups/* refs/heads/other/a:refs/dups/a &&
		but rev-parse --verify refs/dups/a >../actual &&
		but rev-parse --verify refs/dups/b >>../actual &&
		but rev-parse --verify refs/dups/c >>../actual
	) &&
	test_cmp expect actual
'

test_expect_success "push --prune with negative refspec" '
	(
		cd two &&
		but branch prune/a &&
		but branch prune/b &&
		but branch prune/c &&
		but push ../three refs/heads/prune/* &&
		but branch -d prune/a &&
		but branch -d prune/b &&
		but push --prune ../three refs/heads/prune/* ^refs/heads/prune/b
	) &&
	(
		cd three &&
		test_write_lines b c >expect &&
		but for-each-ref --format="%(refname:lstrip=3)" refs/heads/prune/ >actual &&
		test_cmp expect actual
	)
'

test_expect_success "push --prune with negative refspec apply to the destination" '
	(
		cd two &&
		but branch ours/a &&
		but branch ours/b &&
		but branch ours/c &&
		but push ../three refs/heads/ours/*:refs/heads/theirs/* &&
		but branch -d ours/a &&
		but branch -d ours/b &&
		but push --prune ../three refs/heads/ours/*:refs/heads/theirs/* ^refs/heads/theirs/b
	) &&
	(
		cd three &&
		test_write_lines b c >expect &&
		but for-each-ref --format="%(refname:lstrip=3)" refs/heads/theirs/ >actual &&
		test_cmp expect actual
	)
'

test_expect_success "fetch --prune with negative refspec" '
	(
		cd two &&
		but branch fetch/a &&
		but branch fetch/b &&
		but branch fetch/c
	) &&
	(
		cd three &&
		but fetch ../two/.but refs/heads/fetch/*:refs/heads/copied/*
	) &&
	(
		cd two &&
		but branch -d fetch/a &&
		but branch -d fetch/b
	) &&
	(
		cd three &&
		test_write_lines b c >expect &&
		but fetch -v ../two/.but --prune refs/heads/fetch/*:refs/heads/copied/* ^refs/heads/fetch/b &&
		but for-each-ref --format="%(refname:lstrip=3)" refs/heads/copied/ >actual &&
		test_cmp expect actual
	)
'

test_expect_success "push with matching : and negative refspec" '
	# Manually handle cleanup, since test_config is not
	# prepared to take arbitrary options like --add
	test_when_finished "test_unconfig -C two remote.one.push" &&

	# For convenience, we use "master" to refer to the name of
	# the branch created by default in the following.
	#
	# Repositories two and one have branches other than "master"
	# but they have no overlap---"master" is the only one that
	# is shared between them.  And the master branch at two is
	# behind the master branch at one by one cummit.
	but -C two config --add remote.one.push : &&

	# A matching push tries to update master, fails due to non-ff
	test_must_fail but -C two push one &&

	# "master" may actually not be "master"---find it out.
	current=$(but symbolic-ref HEAD) &&

	# If master is in negative refspec, then the command will not attempt
	# to push and succeed.
	but -C two config --add remote.one.push "^$current" &&

	# With "master" excluded, this push is a no-op.  Nothing gets
	# pushed and it succeeds.
	but -C two push -v one
'

test_expect_success "push with matching +: and negative refspec" '
	test_when_finished "test_unconfig -C two remote.one.push" &&

	# The same set-up as above, whose side-effect was a no-op.
	but -C two config --add remote.one.push +: &&

	# The push refuses to update the "master" branch that is checked
	# out in the "one" repository, even when it is forced with +:
	test_must_fail but -C two push one &&

	# "master" may actually not be "master"---find it out.
	current=$(but symbolic-ref HEAD) &&

	# If master is in negative refspec, then the command will not attempt
	# to push and succeed
	but -C two config --add remote.one.push "^$current" &&

	# With "master" excluded, this push is a no-op.  Nothing gets
	# pushed and it succeeds.
	but -C two push -v one
'

test_expect_success '--prefetch correctly modifies refspecs' '
	but -C one config --unset-all remote.origin.fetch &&
	but -C one config --add remote.origin.fetch ^refs/heads/bogus/ignore &&
	but -C one config --add remote.origin.fetch "refs/tags/*:refs/tags/*" &&
	but -C one config --add remote.origin.fetch "refs/heads/bogus/*:bogus/*" &&

	but tag -a -m never never-fetch-tag HEAD &&

	but branch bogus/fetched HEAD~1 &&
	but branch bogus/ignore HEAD &&

	but -C one fetch --prefetch --no-tags &&
	test_must_fail but -C one rev-parse never-fetch-tag &&
	but -C one rev-parse refs/prefetch/bogus/fetched &&
	test_must_fail but -C one rev-parse refs/prefetch/bogus/ignore &&

	# correctly handle when refspec set becomes empty
	# after removing the refs/tags/* refspec.
	but -C one config --unset-all remote.origin.fetch &&
	but -C one config --add remote.origin.fetch "refs/tags/*:refs/tags/*" &&

	but -C one fetch --prefetch --no-tags &&
	test_must_fail but -C one rev-parse never-fetch-tag &&

	# The refspec for refs that are not fully qualified
	# are filtered multiple times.
	but -C one rev-parse refs/prefetch/bogus/fetched &&
	test_must_fail but -C one rev-parse refs/prefetch/bogus/ignore
'

test_expect_success '--prefetch succeeds when refspec becomes empty' '
	but checkout bogus/fetched &&
	test_cummit extra &&

	but -C one config --unset-all remote.origin.fetch &&
	but -C one config --unset branch.main.remote &&
	but -C one config remote.origin.fetch "+refs/tags/extra" &&
	but -C one config remote.origin.skipfetchall true &&
	but -C one config remote.origin.tagopt "--no-tags" &&

	but -C one fetch --prefetch
'

test_done
