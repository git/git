#!/bin/sh

test_description='push from/to a shallow clone'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cummit() {
	echo "$1" >tracked &&
	git add tracked &&
	git cummit -m "$1"
}

test_expect_success 'setup' '
	git config --global transfer.fsckObjects true &&
	cummit 1 &&
	cummit 2 &&
	cummit 3 &&
	cummit 4 &&
	git clone . full &&
	(
	git init full-abc &&
	cd full-abc &&
	cummit a &&
	cummit b &&
	cummit c
	) &&
	git clone --no-local --depth=2 .git shallow &&
	git --git-dir=shallow/.git log --format=%s >actual &&
	cat <<EOF >expect &&
4
3
EOF
	test_cmp expect actual &&
	git clone --no-local --depth=2 full-abc/.git shallow2 &&
	git --git-dir=shallow2/.git log --format=%s >actual &&
	cat <<EOF >expect &&
c
b
EOF
	test_cmp expect actual
'

test_expect_success 'push from shallow clone' '
	(
	cd shallow &&
	cummit 5 &&
	git push ../.git +main:refs/remotes/shallow/main
	) &&
	git log --format=%s shallow/main >actual &&
	git fsck &&
	cat <<EOF >expect &&
5
4
3
2
1
EOF
	test_cmp expect actual
'

test_expect_success 'push from shallow clone, with grafted roots' '
	(
	cd shallow2 &&
	test_must_fail git push ../.git +main:refs/remotes/shallow2/main 2>err &&
	grep "shallow2/main.*shallow update not allowed" err
	) &&
	test_must_fail git rev-parse shallow2/main &&
	git fsck
'

test_expect_success 'add new shallow root with receive.updateshallow on' '
	test_config receive.shallowupdate true &&
	(
	cd shallow2 &&
	git push ../.git +main:refs/remotes/shallow2/main
	) &&
	git log --format=%s shallow2/main >actual &&
	git fsck &&
	cat <<EOF >expect &&
c
b
EOF
	test_cmp expect actual
'

test_expect_success 'push from shallow to shallow' '
	(
	cd shallow &&
	git --git-dir=../shallow2/.git config receive.shallowupdate true &&
	git push ../shallow2/.git +main:refs/remotes/shallow/main &&
	git --git-dir=../shallow2/.git config receive.shallowupdate false
	) &&
	(
	cd shallow2 &&
	git log --format=%s shallow/main >actual &&
	git fsck &&
	cat <<EOF >expect &&
5
4
3
EOF
	test_cmp expect actual
	)
'

test_expect_success 'push from full to shallow' '
	! git --git-dir=shallow2/.git cat-file blob $(echo 1|git hash-object --stdin) &&
	cummit 1 &&
	git push shallow2/.git +main:refs/remotes/top/main &&
	(
	cd shallow2 &&
	git log --format=%s top/main >actual &&
	git fsck &&
	cat <<EOF >expect &&
1
4
3
EOF
	test_cmp expect actual &&
	git cat-file blob $(echo 1|git hash-object --stdin) >/dev/null
	)
'
test_done
