#!/bin/sh

test_description='push from/to a shallow clone'

. ./test-lib.sh

commit() {
	echo "$1" >tracked &&
	git add tracked &&
	git commit -m "$1"
}

test_expect_success 'setup' '
	git config --global transfer.fsckObjects true &&
	commit 1 &&
	commit 2 &&
	commit 3 &&
	commit 4 &&
	(
	git init full-abc &&
	cd full-abc &&
	commit a &&
	commit b &&
	commit c
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
	commit 5 &&
	git push ../.git +master:refs/remotes/shallow/master
	) &&
	git log --format=%s shallow/master >actual &&
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
	test_must_fail git push ../.git +master:refs/remotes/shallow2/master 2>err &&
	grep "shallow2/master.*shallow update not allowed" err
	) &&
	test_must_fail git rev-parse shallow2/master &&
	git fsck
'

test_expect_success 'add new shallow root with receive.updateshallow on' '
	test_config receive.shallowupdate true &&
	(
	cd shallow2 &&
	git push ../.git +master:refs/remotes/shallow2/master
	) &&
	git log --format=%s shallow2/master >actual &&
	git fsck &&
	cat <<EOF >expect &&
c
b
EOF
	test_cmp expect actual
'

test_done
