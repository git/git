#!/bin/sh
#
# Copyright (c) 2008 Johannes E. Schindelin
#

test_description='prune'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

day=$((60*60*24))
week=$(($day*7))

add_blob() {
	before=$(git count-objects | sed "s/ .*//") &&
	BLOB=$(echo aleph_0 | git hash-object -w --stdin) &&
	BLOB_FILE=.git/objects/$(echo $BLOB | sed "s/^../&\//") &&
	verbose test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE &&
	test-tool chmtime =+0 $BLOB_FILE
}

test_expect_success setup '
	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git gc
'

test_expect_success 'prune stale packs' '
	orig_pack=$(echo .git/objects/pack/*.pack) &&
	>.git/objects/tmp_1.pack &&
	>.git/objects/tmp_2.pack &&
	test-tool chmtime =-86501 .git/objects/tmp_1.pack &&
	git prune --expire 1.day &&
	test_path_is_file $orig_pack &&
	test_path_is_file .git/objects/tmp_2.pack &&
	test_path_is_missing .git/objects/tmp_1.pack
'

test_expect_success 'prune --expire' '
	add_blob &&
	git prune --expire=1.hour.ago &&
	verbose test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE &&
	test-tool chmtime =-86500 $BLOB_FILE &&
	git prune --expire 1.day &&
	verbose test $before = $(git count-objects | sed "s/ .*//") &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc: implicit prune --expire' '
	add_blob &&
	test-tool chmtime =-$((2*$week-30)) $BLOB_FILE &&
	git gc &&
	verbose test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE &&
	test-tool chmtime =-$((2*$week+1)) $BLOB_FILE &&
	git gc &&
	verbose test $before = $(git count-objects | sed "s/ .*//") &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc: refuse to start with invalid gc.pruneExpire' '
	git config gc.pruneExpire invalid &&
	test_must_fail git gc
'

test_expect_success 'gc: start with ok gc.pruneExpire' '
	git config gc.pruneExpire 2.days.ago &&
	git gc
'

test_expect_success 'prune: prune nonsense parameters' '
	test_must_fail git prune garbage &&
	test_must_fail git prune --- &&
	test_must_fail git prune --no-such-option
'

test_expect_success 'prune: prune unreachable heads' '
	git config core.logAllRefUpdates false &&
	>file2 &&
	git add file2 &&
	git commit -m temporary &&
	tmp_head=$(git rev-list -1 HEAD) &&
	git reset HEAD^ &&
	git reflog expire --all &&
	git prune &&
	test_must_fail git reset $tmp_head --
'

test_expect_success 'prune: do not prune detached HEAD with no reflog' '
	git checkout --detach --quiet &&
	git commit --allow-empty -m "detached commit" &&
	git reflog expire --all &&
	git prune -n >prune_actual &&
	test_must_be_empty prune_actual
'

test_expect_success 'prune: prune former HEAD after checking out branch' '
	head_oid=$(git rev-parse HEAD) &&
	git checkout --quiet main &&
	git reflog expire --all &&
	git prune -v >prune_actual &&
	grep "$head_oid" prune_actual
'

test_expect_success 'prune: do not prune heads listed as an argument' '
	>file2 &&
	git add file2 &&
	git commit -m temporary &&
	tmp_head=$(git rev-list -1 HEAD) &&
	git reset HEAD^ &&
	git prune -- $tmp_head &&
	git reset $tmp_head --
'

test_expect_success 'gc --no-prune' '
	add_blob &&
	test-tool chmtime =-$((5001*$day)) $BLOB_FILE &&
	git config gc.pruneExpire 2.days.ago &&
	git gc --no-prune &&
	verbose test 1 = $(git count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE
'

test_expect_success 'gc respects gc.pruneExpire' '
	git config gc.pruneExpire 5002.days.ago &&
	git gc &&
	test_path_is_file $BLOB_FILE &&
	git config gc.pruneExpire 5000.days.ago &&
	git gc &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc --prune=<date>' '
	add_blob &&
	test-tool chmtime =-$((5001*$day)) $BLOB_FILE &&
	git gc --prune=5002.days.ago &&
	test_path_is_file $BLOB_FILE &&
	git gc --prune=5000.days.ago &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc --prune=never' '
	add_blob &&
	git gc --prune=never &&
	test_path_is_file $BLOB_FILE &&
	git gc --prune=now &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc respects gc.pruneExpire=never' '
	git config gc.pruneExpire never &&
	add_blob &&
	git gc &&
	test_path_is_file $BLOB_FILE &&
	git config gc.pruneExpire now &&
	git gc &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'prune --expire=never' '
	add_blob &&
	git prune --expire=never &&
	test_path_is_file $BLOB_FILE &&
	git prune &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc: prune old objects after local clone' '
	add_blob &&
	test-tool chmtime =-$((2*$week+1)) $BLOB_FILE &&
	git clone --no-hardlinks . aclone &&
	(
		cd aclone &&
		verbose test 1 = $(git count-objects | sed "s/ .*//") &&
		test_path_is_file $BLOB_FILE &&
		git gc --prune &&
		verbose test 0 = $(git count-objects | sed "s/ .*//") &&
		test_path_is_missing $BLOB_FILE
	)
'

test_expect_success 'garbage report in count-objects -v' '
	test_when_finished "rm -f .git/objects/pack/fake*" &&
	test_when_finished "rm -f .git/objects/pack/foo*" &&
	>.git/objects/pack/foo &&
	>.git/objects/pack/foo.bar &&
	>.git/objects/pack/foo.keep &&
	>.git/objects/pack/foo.pack &&
	>.git/objects/pack/fake.bar &&
	>.git/objects/pack/fake.keep &&
	>.git/objects/pack/fake.pack &&
	>.git/objects/pack/fake.idx &&
	>.git/objects/pack/fake2.keep &&
	>.git/objects/pack/fake3.idx &&
	git count-objects -v 2>stderr &&
	grep "index file .git/objects/pack/fake.idx is too small" stderr &&
	grep "^warning:" stderr | sort >actual &&
	cat >expected <<\EOF &&
warning: garbage found: .git/objects/pack/fake.bar
warning: garbage found: .git/objects/pack/foo
warning: garbage found: .git/objects/pack/foo.bar
warning: no corresponding .idx or .pack: .git/objects/pack/fake2.keep
warning: no corresponding .idx: .git/objects/pack/foo.keep
warning: no corresponding .idx: .git/objects/pack/foo.pack
warning: no corresponding .pack: .git/objects/pack/fake3.idx
EOF
	test_cmp expected actual
'

test_expect_success 'clean pack garbage with gc' '
	test_when_finished "rm -f .git/objects/pack/fake*" &&
	test_when_finished "rm -f .git/objects/pack/foo*" &&
	>.git/objects/pack/foo.keep &&
	>.git/objects/pack/foo.pack &&
	>.git/objects/pack/fake.idx &&
	>.git/objects/pack/fake2.keep &&
	>.git/objects/pack/fake2.idx &&
	>.git/objects/pack/fake3.keep &&
	git gc &&
	git count-objects -v 2>stderr &&
	grep "^warning:" stderr | sort >actual &&
	cat >expected <<\EOF &&
warning: no corresponding .idx or .pack: .git/objects/pack/fake3.keep
warning: no corresponding .idx: .git/objects/pack/foo.keep
warning: no corresponding .idx: .git/objects/pack/foo.pack
EOF
	test_cmp expected actual
'

test_expect_success 'prune .git/shallow' '
	oid=$(echo hi|git commit-tree HEAD^{tree}) &&
	echo $oid >.git/shallow &&
	git prune --dry-run >out &&
	grep $oid .git/shallow &&
	grep $oid out &&
	git prune &&
	test_path_is_missing .git/shallow
'

test_expect_success 'prune .git/shallow when there are no loose objects' '
	oid=$(echo hi|git commit-tree HEAD^{tree}) &&
	echo $oid >.git/shallow &&
	git update-ref refs/heads/shallow-tip $oid &&
	git repack -ad &&
	# verify assumption that all loose objects are gone
	git count-objects | grep ^0 &&
	git prune &&
	echo $oid >expect &&
	test_cmp expect .git/shallow
'

test_expect_success 'prune: handle alternate object database' '
	test_create_repo A &&
	git -C A commit --allow-empty -m "initial commit" &&
	git clone --shared A B &&
	git -C B commit --allow-empty -m "next commit" &&
	git -C B prune
'

test_expect_success 'prune: handle index in multiple worktrees' '
	git worktree add second-worktree &&
	echo "new blob for second-worktree" >second-worktree/blob &&
	git -C second-worktree add blob &&
	git prune --expire=now &&
	git -C second-worktree show :blob >actual &&
	test_cmp second-worktree/blob actual
'

test_expect_success 'prune: handle HEAD in multiple worktrees' '
	git worktree add --detach third-worktree &&
	echo "new blob for third-worktree" >third-worktree/blob &&
	git -C third-worktree add blob &&
	git -C third-worktree commit -m "third" &&
	rm .git/worktrees/third-worktree/index &&
	test_must_fail git -C third-worktree show :blob &&
	git prune --expire=now &&
	git -C third-worktree show HEAD:blob >actual &&
	test_cmp third-worktree/blob actual
'

test_expect_success 'prune: handle HEAD reflog in multiple worktrees' '
	git config core.logAllRefUpdates true &&
	echo "lost blob for third-worktree" >expected &&
	(
		cd third-worktree &&
		cat ../expected >blob &&
		git add blob &&
		git commit -m "second commit in third" &&
		git reset --hard HEAD^
	) &&
	git prune --expire=now &&
	oid=`git hash-object expected` &&
	git -C third-worktree show "$oid" >actual &&
	test_cmp expected actual
'

test_expect_success 'prune: handle expire option correctly' '
	test_must_fail git prune --expire 2>error &&
	test_i18ngrep "requires a value" error &&

	test_must_fail git prune --expire=nyah 2>error &&
	test_i18ngrep "malformed expiration" error &&

	git prune --no-expire
'

test_expect_success 'trivial prune with bitmaps enabled' '
	git repack -adb &&
	blob=$(echo bitmap-unreachable-blob | git hash-object -w --stdin) &&
	git prune --expire=now &&
	git cat-file -e HEAD &&
	test_must_fail git cat-file -e $blob
'

test_expect_success 'old reachable-from-recent retained with bitmaps' '
	git repack -adb &&
	to_drop=$(echo bitmap-from-recent-1 | git hash-object -w --stdin) &&
	test-tool chmtime -86400 .git/objects/$(test_oid_to_path $to_drop) &&
	to_save=$(echo bitmap-from-recent-2 | git hash-object -w --stdin) &&
	test-tool chmtime -86400 .git/objects/$(test_oid_to_path $to_save) &&
	tree=$(printf "100644 blob $to_save\tfile\n" | git mktree) &&
	test-tool chmtime -86400 .git/objects/$(test_oid_to_path $tree) &&
	commit=$(echo foo | git commit-tree $tree) &&
	git prune --expire=12.hours.ago &&
	git cat-file -e $commit &&
	git cat-file -e $tree &&
	git cat-file -e $to_save &&
	test_must_fail git cat-file -e $to_drop
'

test_done
