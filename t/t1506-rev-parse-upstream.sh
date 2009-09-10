#!/bin/sh

test_description='test <branch>@{upstream} syntax'

. ./test-lib.sh


test_expect_success 'setup' '

	test_commit 1 &&
	git checkout -b side &&
	test_commit 2 &&
	git checkout master &&
	git clone . clone &&
	test_commit 3 &&
	(cd clone &&
	 test_commit 4 &&
	 git branch --track my-side origin/side)

'

full_name () {
	(cd clone &&
	 git rev-parse --symbolic-full-name "$@")
}

commit_subject () {
	(cd clone &&
	 git show -s --pretty=format:%s "$@")
}

test_expect_success '@{upstream} resolves to correct full name' '
	test refs/remotes/origin/master = "$(full_name @{upstream})"
'

test_expect_success '@{u} resolves to correct full name' '
	test refs/remotes/origin/master = "$(full_name @{u})"
'

test_expect_success 'my-side@{upstream} resolves to correct full name' '
	test refs/remotes/origin/side = "$(full_name my-side@{u})"
'

test_expect_success 'my-side@{u} resolves to correct commit' '
	git checkout side &&
	test_commit 5 &&
	(cd clone && git fetch) &&
	test 2 = "$(commit_subject my-side)" &&
	test 5 = "$(commit_subject my-side@{u})"
'

test_expect_success 'not-tracking@{u} fails' '
	test_must_fail full_name non-tracking@{u} &&
	(cd clone && git checkout --no-track -b non-tracking) &&
	test_must_fail full_name non-tracking@{u}
'

test_expect_success '<branch>@{u}@{1} resolves correctly' '
	test_commit 6 &&
	(cd clone && git fetch) &&
	test 5 = $(commit_subject my-side@{u}@{1})
'

test_expect_success '@{u} without specifying branch fails on a detached HEAD' '
	git checkout HEAD^0 &&
	test_must_fail git rev-parse @{u}
'

test_done
