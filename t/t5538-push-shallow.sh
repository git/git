#!/bin/sh

test_description='push from/to a shallow clone'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
	git clone . full &&
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
	commit 1 &&
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

test_expect_success 'push new commit from shallow clone has correct object count' '
	git init origin &&
	test_commit -C origin a &&
	test_commit -C origin b &&

	git clone --depth=1 "file://$(pwd)/origin" client &&
	git -C client checkout -b topic &&
	git -C client commit --allow-empty -m "empty" &&
	GIT_PROGRESS_DELAY=0 git -C client push --progress origin topic 2>err &&
	test_grep "Enumerating objects: 1, done." err
'

test_expect_success 'push new commit from shallow clone has good deltas' '
	git init base &&
	test_seq 1 999 >base/a &&
	test_commit -C base initial &&
	git -C base add a &&
	git -C base commit -m "big a" &&

	git clone --depth=1 "file://$(pwd)/base" deltas &&
	git -C deltas checkout -b deltas &&
	test_seq 1 1000 >deltas/a &&
	git -C deltas commit -a -m "bigger a" &&
	GIT_PROGRESS_DELAY=0 git -C deltas push --progress origin deltas 2>err &&

	test_grep "Enumerating objects: 5, done" err &&

	# If the delta base is found, then this message uses "bytes".
	# If the delta base is not found, then this message uses "KiB".
	test_grep "Writing objects: .* bytes" err &&

	git -C deltas commit --amend -m "changed message" &&
	GIT_TRACE2_EVENT="$(pwd)/config-push.txt" \
	GIT_PROGRESS_DELAY=0 git -C deltas -c pack.usePathWalk=true \
		push --progress -f origin deltas 2>err &&

	test_grep "Enumerating objects: 1, done" err &&
	test_region pack-objects path-walk config-push.txt
'

test_done
