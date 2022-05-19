#!/bin/sh

test_description='test <branch>@{upstream} syntax'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


test_expect_success 'setup' '

	test_cummit 1 &&
	but checkout -b side &&
	test_cummit 2 &&
	but checkout main &&
	but clone . clone &&
	test_cummit 3 &&
	(cd clone &&
	 test_cummit 4 &&
	 but branch --track my-side origin/side &&
	 but branch --track local-main main &&
	 but branch --track fun@ny origin/side &&
	 but branch --track @funny origin/side &&
	 but branch --track funny@ origin/side &&
	 but remote add -t main main-only .. &&
	 but fetch main-only &&
	 but branch bad-upstream &&
	 but config branch.bad-upstream.remote main-only &&
	 but config branch.bad-upstream.merge refs/heads/side
	)
'

cummit_subject () {
	(cd clone &&
	 but show -s --pretty=tformat:%s "$@")
}

error_message () {
	(cd clone &&
	 test_must_fail but rev-parse --verify "$@" 2>../error)
}

test_expect_success '@{upstream} resolves to correct full name' '
	echo refs/remotes/origin/main >expect &&
	but -C clone rev-parse --symbolic-full-name @{upstream} >actual &&
	test_cmp expect actual &&
	but -C clone rev-parse --symbolic-full-name @{UPSTREAM} >actual &&
	test_cmp expect actual &&
	but -C clone rev-parse --symbolic-full-name @{UpSTReam} >actual &&
	test_cmp expect actual
'

test_expect_success '@{u} resolves to correct full name' '
	echo refs/remotes/origin/main >expect &&
	but -C clone rev-parse --symbolic-full-name @{u} >actual &&
	test_cmp expect actual &&
	but -C clone rev-parse --symbolic-full-name @{U} >actual &&
	test_cmp expect actual
'

test_expect_success 'my-side@{upstream} resolves to correct full name' '
	echo refs/remotes/origin/side >expect &&
	but -C clone rev-parse --symbolic-full-name my-side@{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ in middle' '
	but -C clone rev-parse --symbolic-full-name fun@ny@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual &&
	but -C clone rev-parse --symbolic-full-name fun@ny@{U} >actual &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ at start' '
	but -C clone rev-parse --symbolic-full-name @funny@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual
'

test_expect_success 'upstream of branch with @ at end' '
	but -C clone rev-parse --symbolic-full-name funny@@{u} >actual &&
	echo refs/remotes/origin/side >expect &&
	test_cmp expect actual
'

test_expect_success 'refs/heads/my-side@{upstream} does not resolve to my-side{upstream}' '
	test_must_fail but -C clone rev-parse --symbolic-full-name refs/heads/my-side@{upstream}
'

test_expect_success 'my-side@{u} resolves to correct cummit' '
	but checkout side &&
	test_cummit 5 &&
	(cd clone && but fetch) &&
	echo 2 >expect &&
	cummit_subject my-side >actual &&
	test_cmp expect actual &&
	echo 5 >expect &&
	cummit_subject my-side@{u} >actual
'

test_expect_success 'not-tracking@{u} fails' '
	test_must_fail but -C clone rev-parse --symbolic-full-name non-tracking@{u} &&
	(cd clone && but checkout --no-track -b non-tracking) &&
	test_must_fail but -C clone rev-parse --symbolic-full-name non-tracking@{u}
'

test_expect_success '<branch>@{u}@{1} resolves correctly' '
	test_cummit 6 &&
	(cd clone && but fetch) &&
	echo 5 >expect &&
	cummit_subject my-side@{u}@{1} >actual &&
	test_cmp expect actual &&
	cummit_subject my-side@{U}@{1} >actual &&
	test_cmp expect actual
'

test_expect_success '@{u} without specifying branch fails on a detached HEAD' '
	but checkout HEAD^0 &&
	test_must_fail but rev-parse @{u} &&
	test_must_fail but rev-parse @{U}
'

test_expect_success 'checkout -b new my-side@{u} forks from the same' '
(
	cd clone &&
	but checkout -b new my-side@{u} &&
	but rev-parse --symbolic-full-name my-side@{u} >expect &&
	but rev-parse --symbolic-full-name new@{u} >actual &&
	test_cmp expect actual
)
'

test_expect_success 'merge my-side@{u} records the correct name' '
(
	cd clone &&
	but checkout main &&
	test_might_fail but branch -D new &&
	but branch -t new my-side@{u} &&
	but merge -s ours new@{u} &&
	but show -s --pretty=tformat:%s >actual &&
	echo "Merge remote-tracking branch ${SQ}origin/side${SQ}" >expect &&
	test_cmp expect actual
)
'

test_expect_success 'branch -d other@{u}' '
	but checkout -t -b other main &&
	but branch -d @{u} &&
	but for-each-ref refs/heads/main >actual &&
	test_must_be_empty actual
'

test_expect_success 'checkout other@{u}' '
	but branch -f main HEAD &&
	but checkout -t -b another main &&
	but checkout @{u} &&
	but symbolic-ref HEAD >actual &&
	echo refs/heads/main >expect &&
	test_cmp expect actual
'

test_expect_success 'branch@{u} works when tracking a local branch' '
	echo refs/heads/main >expect &&
	but -C clone rev-parse --symbolic-full-name local-main@{u} >actual &&
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
	test_must_fail but rev-parse --verify @{u} 2>actual &&
	test_cmp expect actual
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
	but checkout HEAD^0 &&
	test_must_fail but rev-parse --verify @{u} 2>actual &&
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
	but checkout local-main &&
	but pull
)
'

# makes sense if the previous one succeeded
test_expect_success '@{u} works when tracking a local branch' '
	echo refs/heads/main >expect &&
	but -C clone rev-parse --symbolic-full-name @{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'log -g other@{u}' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
	cummit $cummit
	Reflog: main@{0} (C O Mitter <cummitter@example.com>)
	Reflog message: branch: Created from HEAD
	Author: A U Thor <author@example.com>
	Date:   Thu Apr 7 15:15:13 2005 -0700

	    3
	EOF
	but log -1 -g other@{u} >actual &&
	test_cmp expect actual
'

test_expect_success 'log -g other@{u}@{now}' '
	cummit=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
	cummit $cummit
	Reflog: main@{Thu Apr 7 15:17:13 2005 -0700} (C O Mitter <cummitter@example.com>)
	Reflog message: branch: Created from HEAD
	Author: A U Thor <author@example.com>
	Date:   Thu Apr 7 15:15:13 2005 -0700

	    3
	EOF
	but log -1 -g other@{u}@{now} >actual &&
	test_cmp expect actual
'

test_expect_success '@{reflog}-parsing does not look beyond colon' '
	echo content >@{yesterday} &&
	but add @{yesterday} &&
	but cummit -m "funny reflog file" &&
	but hash-object @{yesterday} >expect &&
	but rev-parse HEAD:@{yesterday} >actual
'

test_expect_success '@{upstream}-parsing does not look beyond colon' '
	echo content >@{upstream} &&
	but add @{upstream} &&
	but cummit -m "funny upstream file" &&
	but hash-object @{upstream} >expect &&
	but rev-parse HEAD:@{upstream} >actual
'

test_done
