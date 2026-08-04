#!/bin/sh

test_description='tests for git-history squash subcommand'

. ./test-lib.sh

test_expect_success 'setup linear history' '
	test_commit base file a start &&
	test_commit one file b &&
	test_commit two file c &&
	test_commit three file d
'

test_expect_success 'errors on missing range argument' '
	test_must_fail git history squash 2>err &&
	test_grep "expects a revision range" err
'

test_expect_success 'errors on an empty range' '
	test_must_fail git history squash HEAD..HEAD 2>err &&
	test_grep "the revision range is empty" err
'

test_expect_success 'errors on a single revision that is not a range' '
	test_must_fail git history squash HEAD 2>err &&
	test_grep "not a .*range" err &&
	test_must_fail git history squash HEAD~1 2>err &&
	test_grep "not a .*range" err
'

test_expect_success 'errors on a range holding a single commit' '
	test_must_fail git history squash "HEAD^!" 2>err &&
	test_grep "single commit; nothing to squash" err
'

test_expect_success 'rejects a root commit' '
	oid=$(git commit-tree -m root three^{tree}) &&
	test_must_fail git history squash \
		--ancestry-path=start "$oid..three" 2>err &&
	test_grep "cannot squash down to root commit" err
'

test_expect_success 'rejects multiple tips' '
	oid=$(git commit-tree -m tip -p start^0 three^{tree}) &&
	test_must_fail git history squash ^start "$oid" three~1 2>err &&
	test_grep "revision range contains more than one tip" err
'

test_expect_success 'rejects a merge parent outside the range' '
	git reset --hard start &&
	main=$(git symbolic-ref --short HEAD) &&
	git checkout -b outside-parent &&
	test_commit --no-tag outside-parent outside x &&
	git checkout "$main" &&
	test_commit --no-tag outside-main file b &&
	base=$(git rev-parse HEAD) &&
	test_commit --no-tag outside-mid file c &&
	git merge --no-ff -m "merge outside-parent" outside-parent &&
	git branch -D outside-parent &&

	test_must_fail git history squash "$base.." 2>err &&
	test_grep "parent .* of commit .* is outside the revision range" err
'

test_done
