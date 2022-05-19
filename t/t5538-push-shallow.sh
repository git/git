#!/bin/sh

test_description='push from/to a shallow clone'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cummit() {
	echo "$1" >tracked &&
	but add tracked &&
	but cummit -m "$1"
}

test_expect_success 'setup' '
	but config --global transfer.fsckObjects true &&
	cummit 1 &&
	cummit 2 &&
	cummit 3 &&
	cummit 4 &&
	but clone . full &&
	(
	but init full-abc &&
	cd full-abc &&
	cummit a &&
	cummit b &&
	cummit c
	) &&
	but clone --no-local --depth=2 .but shallow &&
	but --but-dir=shallow/.but log --format=%s >actual &&
	cat <<EOF >expect &&
4
3
EOF
	test_cmp expect actual &&
	but clone --no-local --depth=2 full-abc/.but shallow2 &&
	but --but-dir=shallow2/.but log --format=%s >actual &&
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
	but push ../.but +main:refs/remotes/shallow/main
	) &&
	but log --format=%s shallow/main >actual &&
	but fsck &&
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
	test_must_fail but push ../.but +main:refs/remotes/shallow2/main 2>err &&
	grep "shallow2/main.*shallow update not allowed" err
	) &&
	test_must_fail but rev-parse shallow2/main &&
	but fsck
'

test_expect_success 'add new shallow root with receive.updateshallow on' '
	test_config receive.shallowupdate true &&
	(
	cd shallow2 &&
	but push ../.but +main:refs/remotes/shallow2/main
	) &&
	but log --format=%s shallow2/main >actual &&
	but fsck &&
	cat <<EOF >expect &&
c
b
EOF
	test_cmp expect actual
'

test_expect_success 'push from shallow to shallow' '
	(
	cd shallow &&
	but --but-dir=../shallow2/.but config receive.shallowupdate true &&
	but push ../shallow2/.but +main:refs/remotes/shallow/main &&
	but --but-dir=../shallow2/.but config receive.shallowupdate false
	) &&
	(
	cd shallow2 &&
	but log --format=%s shallow/main >actual &&
	but fsck &&
	cat <<EOF >expect &&
5
4
3
EOF
	test_cmp expect actual
	)
'

test_expect_success 'push from full to shallow' '
	! but --but-dir=shallow2/.but cat-file blob $(echo 1|but hash-object --stdin) &&
	cummit 1 &&
	but push shallow2/.but +main:refs/remotes/top/main &&
	(
	cd shallow2 &&
	but log --format=%s top/main >actual &&
	but fsck &&
	cat <<EOF >expect &&
1
4
3
EOF
	test_cmp expect actual &&
	but cat-file blob $(echo 1|but hash-object --stdin) >/dev/null
	)
'
test_done
