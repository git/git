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
	 git branch --track my-side origin/side &&
	 git branch --track local-master master &&
	 git branch --track fun@ny origin/side &&
	 git branch --track @funny origin/side &&
	 git branch --track funny@ origin/side &&
	 git remote add -t master master-only .. &&
	 git fetch master-only &&
	 git branch bad-upstream &&
	 git config branch.bad-upstream.remote master-only &&
	 git config branch.bad-upstream.merge refs/heads/side
	)
'

full_name () {
	(cd clone &&
	 git rev-parse --symbolic-full-name "$@")
}

commit_subject () {
	(cd clone &&
	 git show -s --pretty=format:%s "$@")
}

error_message () {
	(cd clone &&
	 test_must_fail git rev-parse --verify "$@" 2>../error)
}

test_expect_success '@{upstream} resolves to correct full name' '
	test refs/remotes/origin/master = "$(full_name @{upstream})" &&
	test refs/remotes/origin/master = "$(full_name @{UPSTREAM})" &&
	test refs/remotes/origin/master = "$(full_name @{UpSTReam})"
'

test_expect_success '@{u} resolves to correct full name' '
	test refs/remotes/origin/master = "$(full_name @{u})" &&
	test refs/remotes/origin/master = "$(full_name @{U})"
'

test_expect_success 'my-side@{upstream} resolves to correct full name' '
	test refs/remotes/origin/side = "$(full_name my-side@{u})"
'

test_expect_success 'upstream of branch with @ in middle' '
	full_name fun@ny@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual &&
	full_name fun@ny@{U} >actual &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ at start' '
	full_name @funny@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ at end' '
	full_name funny@@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual
'

test_expect_success 'refs/heads/my-side@{upstream} does not resolve to my-side{upstream}' '
	test_must_fail full_name refs/heads/my-side@{upstream}
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
	test 5 = $(commit_subject my-side@{u}@{1}) &&
	test 5 = $(commit_subject my-side@{U}@{1})
'

test_expect_success '@{u} without specifying branch fails on a detached HEAD' '
	git checkout HEAD^0 &&
	test_must_fail git rev-parse @{u} &&
	test_must_fail git rev-parse @{U}
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
	cd clone &&
	git checkout master &&
	test_might_fail git branch -D new &&
	git branch -t new my-side@{u} &&
	git merge -s ours new@{u} &&
	git show -s --pretty=tformat:%s >actual &&
	echo "Merge remote-tracking branch ${SQ}origin/side${SQ}" >expect &&
	test_cmp expect actual
)
'

test_expect_success 'branch -d other@{u}' '
	git checkout -t -b other master &&
	git branch -d @{u} &&
	git for-each-ref refs/heads/master >actual &&
	test_must_be_empty actual
'

test_expect_success 'checkout other@{u}' '
	git branch -f master HEAD &&
	git checkout -t -b another master &&
	git checkout @{u} &&
	git symbolic-ref HEAD >actual &&
	echo refs/heads/master >expect &&
	test_cmp expect actual
'

test_expect_success 'branch@{u} works when tracking a local branch' '
	test refs/heads/master = "$(full_name local-master@{u})"
'

test_expect_success 'branch@{u} error message when no upstream' '
	cat >expect <<-EOF &&
	fatal: no upstream configured for branch ${SQ}non-tracking${SQ}
	EOF
	error_message non-tracking@{u} &&
	test_i18ncmp expect error
'

test_expect_success '@{u} error message when no upstream' '
	cat >expect <<-EOF &&
	fatal: no upstream configured for branch ${SQ}master${SQ}
	EOF
	test_must_fail git rev-parse --verify @{u} 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'branch@{u} error message with misspelt branch' '
	cat >expect <<-EOF &&
	fatal: no such branch: ${SQ}no-such-branch${SQ}
	EOF
	error_message no-such-branch@{u} &&
	test_i18ncmp expect error
'

test_expect_success '@{u} error message when not on a branch' '
	cat >expect <<-EOF &&
	fatal: HEAD does not point to a branch
	EOF
	git checkout HEAD^0 &&
	test_must_fail git rev-parse --verify @{u} 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'branch@{u} error message if upstream branch not fetched' '
	cat >expect <<-EOF &&
	fatal: upstream branch ${SQ}refs/heads/side${SQ} not stored as a remote-tracking branch
	EOF
	error_message bad-upstream@{u} &&
	test_i18ncmp expect error
'

test_expect_success 'pull works when tracking a local branch' '
(
	cd clone &&
	git checkout local-master &&
	git pull
)
'

# makes sense if the previous one succeeded
test_expect_success '@{u} works when tracking a local branch' '
	test refs/heads/master = "$(full_name @{u})"
'

commit=$(git rev-parse HEAD)
cat >expect <<EOF
commit $commit
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
commit $commit
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

test_expect_success '@{reflog}-parsing does not look beyond colon' '
	echo content >@{yesterday} &&
	git add @{yesterday} &&
	git commit -m "funny reflog file" &&
	git hash-object @{yesterday} >expect &&
	git rev-parse HEAD:@{yesterday} >actual
'

test_expect_success '@{upstream}-parsing does not look beyond colon' '
	echo content >@{upstream} &&
	git add @{upstream} &&
	git commit -m "funny upstream file" &&
	git hash-object @{upstream} >expect &&
	git rev-parse HEAD:@{upstream} >actual
'

test_done
