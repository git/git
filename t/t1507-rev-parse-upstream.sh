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

test_expect_success 'checkout -b new my-side@{u} forks from the same' '
(
	cd clone &&
	git checkout -b new my-side@{u} &&
	git rev-parse --symbolic-full-name my-side@{u} >expect &&
	git rev-parse --symbolic-full-name new@{u} >actual &&
	test_cmp expect actual
)
'

test_expect_success 'merge my-side@{u} records the correct name' '
(
	sq="'\''" &&
	cd clone || exit
	git checkout master || exit
	git branch -D new ;# can fail but is ok
	git branch -t new my-side@{u} &&
	git merge -s ours new@{u} &&
	git show -s --pretty=format:%s >actual &&
	echo "Merge remote branch ${sq}origin/side${sq}" >expect &&
	test_cmp expect actual
)
'

test_expect_success 'branch -d other@{u}' '
	git checkout -t -b other master &&
	git branch -d @{u} &&
	git for-each-ref refs/heads/master >actual &&
	>expect &&
	test_cmp expect actual
'

test_expect_success 'checkout other@{u}' '
	git branch -f master HEAD &&
	git checkout -t -b another master &&
	git checkout @{u} &&
	git symbolic-ref HEAD >actual &&
	echo refs/heads/master >expect &&
	test_cmp expect actual
'

cat >expect <<EOF
commit 8f489d01d0cc65c3b0f09504ec50b5ed02a70bd5
Reflog: master@{0} (C O Mitter <committer@example.com>)
Reflog message: branch: Created from HEAD
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:15:13 2005 -0700

    3
EOF
test_expect_success 'log -g other@{u}' '
	git log -1 -g other@{u} >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
commit 8f489d01d0cc65c3b0f09504ec50b5ed02a70bd5
Reflog: master@{Thu Apr 7 15:17:13 2005 -0700} (C O Mitter <committer@example.com>)
Reflog message: branch: Created from HEAD
Author: A U Thor <author@example.com>
Date:   Thu Apr 7 15:15:13 2005 -0700

    3
EOF

test_expect_success 'log -g other@{u}@{now}' '
	git log -1 -g other@{u}@{now} >actual &&
	test_cmp expect actual
'

test_done
