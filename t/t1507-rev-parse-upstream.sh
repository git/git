#!/bin/sh

test_description='test <branch>@{upstream} syntax'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


test_expect_success 'setup' '

	test_commit 1 &&
	git checkout -b side &&
	test_commit 2 &&
	git checkout main &&
	git clone . clone &&
	test_commit 3 &&
	(cd clone &&
	 test_commit 4 &&
	 git branch --track my-side origin/side &&
	 git branch --track local-main main &&
	 git branch --track fun@ny origin/side &&
	 git branch --track @funny origin/side &&
	 git branch --track funny@ origin/side &&
	 git remote add -t main main-only .. &&
	 git fetch main-only &&
	 git branch bad-upstream &&
	 git config branch.bad-upstream.remote main-only &&
	 git config branch.bad-upstream.merge refs/heads/side
	)
'

commit_subject () {
	(cd clone &&
	 git show -s --pretty=tformat:%s "$@")
}

error_message () {
	(cd clone &&
	 test_must_fail git rev-parse --verify "$@" 2>../error)
}

test_expect_success '@{upstream} resolves to correct full name' '
	echo refs/remotes/origin/main >expect &&
	git -C clone rev-parse --symbolic-full-name @{upstream} >actual &&
	test_cmp expect actual &&
	git -C clone rev-parse --symbolic-full-name @{UPSTREAM} >actual &&
	test_cmp expect actual &&
	git -C clone rev-parse --symbolic-full-name @{UpSTReam} >actual &&
	test_cmp expect actual
'

test_expect_success '@{u} resolves to correct full name' '
	echo refs/remotes/origin/main >expect &&
	git -C clone rev-parse --symbolic-full-name @{u} >actual &&
	test_cmp expect actual &&
	git -C clone rev-parse --symbolic-full-name @{U} >actual &&
	test_cmp expect actual
'

test_expect_success 'my-side@{upstream} resolves to correct full name' '
	echo refs/remotes/origin/side >expect &&
	git -C clone rev-parse --symbolic-full-name my-side@{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ in middle' '
	git -C clone rev-parse --symbolic-full-name fun@ny@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual &&
	git -C clone rev-parse --symbolic-full-name fun@ny@{U} >actual &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ at start' '
	git -C clone rev-parse --symbolic-full-name @funny@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ at end' '
	git -C clone rev-parse --symbolic-full-name funny@@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual
'

test_expect_success 'refs/heads/my-side@{upstream} does not resolve to my-side{upstream}' '
	test_must_fail git -C clone rev-parse --symbolic-full-name refs/heads/my-side@{upstream}
'

test_expect_success 'my-side@{u} resolves to correct commit' '
	git checkout side &&
	test_commit 5 &&
	(cd clone && git fetch) &&
	echo 2 >expect &&
	commit_subject my-side >actual &&
	test_cmp expect actual &&
	echo 5 >expect &&
	commit_subject my-side@{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'not-tracking@{u} fails' '
	test_must_fail git -C clone rev-parse --symbolic-full-name non-tracking@{u} &&
	(cd clone && git checkout --no-track -b non-tracking) &&
	test_must_fail git -C clone rev-parse --symbolic-full-name non-tracking@{u}
'

test_expect_success '<branch>@{u}@{1} resolves correctly' '
	test_commit 6 &&
	(cd clone && git fetch) &&
	echo 5 >expect &&
	commit_subject my-side@{u}@{1} >actual &&
	test_cmp expect actual &&
	commit_subject my-side@{U}@{1} >actual &&
	test_cmp expect actual
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
	git checkout main &&
	test_might_fail git branch -D new &&
	git branch -t new my-side@{u} &&
	git merge -s ours new@{u} &&
	git show -s --pretty=tformat:%s >actual &&
	echo "Merge remote-tracking branch ${SQ}origin/side${SQ}" >expect &&
	test_cmp expect actual
)
'

test_expect_success 'branch -d other@{u}' '
	git checkout -t -b other main &&
	git branch -d @{u} &&
	git for-each-ref refs/heads/main >actual &&
	test_must_be_empty actual
'

test_expect_success 'checkout other@{u}' '
	git branch -f main HEAD &&
	git checkout -t -b another main &&
	git checkout @{u} &&
	git symbolic-ref HEAD >actual &&
	echo refs/heads/main >expect &&
	test_cmp expect actual
'

test_expect_success 'branch@{u} works when tracking a local branch' '
	echo refs/heads/main >expect &&
	git -C clone rev-parse --symbolic-full-name local-main@{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'branch@{u} error message when no upstream' '
	cat >expect <<-EOF &&
	fatal: no upstream configured for branch ${SQ}non-tracking${SQ}
	EOF
	error_message non-tracking@{u} &&
	test_cmp expect error
'

test_expect_success '@{u} error message when no upstream' '
	cat >expect <<-EOF &&
	fatal: no upstream configured for branch ${SQ}main${SQ}
	EOF
	test_must_fail git rev-parse --verify @{u} 2>actual &&
	test_cmp expect actual
'

test_expect_success '@{u} silent error when no upstream' '
	test_must_fail git rev-parse --verify --quiet @{u} 2>actual &&
	test_must_be_empty actual
'

test_expect_success 'branch@{u} error message with misspelt branch' '
	cat >expect <<-EOF &&
	fatal: no such branch: ${SQ}no-such-branch${SQ}
	EOF
	error_message no-such-branch@{u} &&
	test_cmp expect error
'

test_expect_success '@{u} error message when not on a branch' '
	cat >expect <<-EOF &&
	fatal: HEAD does not point to a branch
	EOF
	git checkout HEAD^0 &&
	test_must_fail git rev-parse --verify @{u} 2>actual &&
	test_cmp expect actual
'

test_expect_success 'branch@{u} error message if upstream branch not fetched' '
	cat >expect <<-EOF &&
	fatal: upstream branch ${SQ}refs/heads/side${SQ} not stored as a remote-tracking branch
	EOF
	error_message bad-upstream@{u} &&
	test_cmp expect error
'

test_expect_success 'pull works when tracking a local branch' '
(
	cd clone &&
	git checkout local-main &&
	git pull
)
'

# makes sense if the previous one succeeded
test_expect_success '@{u} works when tracking a local branch' '
	echo refs/heads/main >expect &&
	git -C clone rev-parse --symbolic-full-name @{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'log -g other@{u}' '
	commit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	commit $commit
	Reflog: main@{0} (C O Mitter <committer@example.com>)
	Reflog message: branch: Created from HEAD
	Author: A U Thor <author@example.com>
	Date:   Thu Apr 7 15:15:13 2005 -0700

	    3
	EOF
	git log -1 -g other@{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'log -g other@{u}@{now}' '
	commit=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	commit $commit
	Reflog: main@{Thu Apr 7 15:17:13 2005 -0700} (C O Mitter <committer@example.com>)
	Reflog message: branch: Created from HEAD
	Author: A U Thor <author@example.com>
	Date:   Thu Apr 7 15:15:13 2005 -0700

	    3
	EOF
	git log -1 -g other@{u}@{now} >actual &&
	test_cmp expect actual
'

test_expect_success '@{reflog}-parsing does not look beyond colon' '
	echo content >@{yesterday} &&
	git add @{yesterday} &&
	git commit -m "funny reflog file" &&
	git hash-object @{yesterday} >expect &&
	git rev-parse HEAD:@{yesterday} >actual &&
	test_cmp expect actual
'

test_expect_success '@{upstream}-parsing does not look beyond colon' '
	echo content >@{upstream} &&
	git add @{upstream} &&
	git commit -m "funny upstream file" &&
	git hash-object @{upstream} >expect &&
	git rev-parse HEAD:@{upstream} >actual &&
	test_cmp expect actual
'

test_done
