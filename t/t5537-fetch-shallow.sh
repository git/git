#!/bin/sh

test_description='fetch/clone from a shallow clone'

. ./test-lib.sh

commit() {
	echo "$1" >tracked &&
	git add tracked &&
	git commit -m "$1"
}

test_expect_success 'setup' '
	commit 1 &&
	commit 2 &&
	commit 3 &&
	commit 4 &&
	git config --global transfer.fsckObjects true
'

test_expect_success 'setup shallow clone' '
	git clone --no-local --depth=2 .git shallow &&
	git --git-dir=shallow/.git log --format=%s >actual &&
	cat <<EOF >expect &&
4
3
EOF
	test_cmp expect actual
'

test_expect_success 'clone from shallow clone' '
	git clone --no-local shallow shallow2 &&
	(
	cd shallow2 &&
	git fsck &&
	git log --format=%s >actual &&
	cat <<EOF >expect &&
4
3
EOF
	test_cmp expect actual
	)
'

test_expect_success 'fetch from shallow clone' '
	(
	cd shallow &&
	commit 5
	) &&
	(
	cd shallow2 &&
	git fetch &&
	git fsck &&
	git log --format=%s origin/master >actual &&
	cat <<EOF >expect &&
5
4
3
EOF
	test_cmp expect actual
	)
'

test_expect_success 'fetch --depth from shallow clone' '
	(
	cd shallow &&
	commit 6
	) &&
	(
	cd shallow2 &&
	git fetch --depth=2 &&
	git fsck &&
	git log --format=%s origin/master >actual &&
	cat <<EOF >expect &&
6
5
EOF
	test_cmp expect actual
	)
'

test_expect_success 'fetch something upstream has but hidden by clients shallow boundaries' '
	# the blob "1" is available in .git but hidden by the
	# shallow2/.git/shallow and it should be resent
	! git --git-dir=shallow2/.git cat-file blob `echo 1|git hash-object --stdin` >/dev/null &&
	echo 1 >1.t &&
	git add 1.t &&
	git commit -m add-1-back &&
	(
	cd shallow2 &&
	git fetch ../.git +refs/heads/master:refs/remotes/top/master &&
	git fsck &&
	git log --format=%s top/master >actual &&
	cat <<EOF >expect &&
add-1-back
4
3
EOF
	test_cmp expect actual
	) &&
	git --git-dir=shallow2/.git cat-file blob `echo 1|git hash-object --stdin` >/dev/null

'

test_expect_success 'fetch that requires changes in .git/shallow is filtered' '
	(
	cd shallow &&
	git checkout --orphan no-shallow &&
	commit no-shallow
	) &&
	git init notshallow &&
	(
	cd notshallow &&
	git fetch ../shallow/.git refs/heads/*:refs/remotes/shallow/*&&
	git for-each-ref --format="%(refname)" >actual.refs &&
	cat <<EOF >expect.refs &&
refs/remotes/shallow/no-shallow
EOF
	test_cmp expect.refs actual.refs &&
	git log --format=%s shallow/no-shallow >actual &&
	cat <<EOF >expect &&
no-shallow
EOF
	test_cmp expect actual
	)
'

test_done
