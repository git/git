#!/bin/sh
#
# Copyright (c) 2008 Johannes E. Schindelin
#

test_description='prune'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

day=$((60*60*24))
week=$(($day*7))

add_blob() {
	before=$(but count-objects | sed "s/ .*//") &&
	BLOB=$(echo aleph_0 | but hash-object -w --stdin) &&
	BLOB_FILE=.but/objects/$(echo $BLOB | sed "s/^../&\//") &&
	verbose test $((1 + $before)) = $(but count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE &&
	test-tool chmtime =+0 $BLOB_FILE
}

test_expect_success setup '
	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but gc
'

test_expect_success 'prune stale packs' '
	orig_pack=$(echo .but/objects/pack/*.pack) &&
	>.but/objects/tmp_1.pack &&
	>.but/objects/tmp_2.pack &&
	test-tool chmtime =-86501 .but/objects/tmp_1.pack &&
	but prune --expire 1.day &&
	test_path_is_file $orig_pack &&
	test_path_is_file .but/objects/tmp_2.pack &&
	test_path_is_missing .but/objects/tmp_1.pack
'

test_expect_success 'prune --expire' '
	add_blob &&
	but prune --expire=1.hour.ago &&
	verbose test $((1 + $before)) = $(but count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE &&
	test-tool chmtime =-86500 $BLOB_FILE &&
	but prune --expire 1.day &&
	verbose test $before = $(but count-objects | sed "s/ .*//") &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc: implicit prune --expire' '
	add_blob &&
	test-tool chmtime =-$((2*$week-30)) $BLOB_FILE &&
	but gc &&
	verbose test $((1 + $before)) = $(but count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE &&
	test-tool chmtime =-$((2*$week+1)) $BLOB_FILE &&
	but gc &&
	verbose test $before = $(but count-objects | sed "s/ .*//") &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc: refuse to start with invalid gc.pruneExpire' '
	but config gc.pruneExpire invalid &&
	test_must_fail but gc
'

test_expect_success 'gc: start with ok gc.pruneExpire' '
	but config gc.pruneExpire 2.days.ago &&
	but gc
'

test_expect_success 'prune: prune nonsense parameters' '
	test_must_fail but prune garbage &&
	test_must_fail but prune --- &&
	test_must_fail but prune --no-such-option
'

test_expect_success 'prune: prune unreachable heads' '
	but config core.logAllRefUpdates false &&
	>file2 &&
	but add file2 &&
	but cummit -m temporary &&
	tmp_head=$(but rev-list -1 HEAD) &&
	but reset HEAD^ &&
	but reflog expire --all &&
	but prune &&
	test_must_fail but reset $tmp_head --
'

test_expect_success 'prune: do not prune detached HEAD with no reflog' '
	but checkout --detach --quiet &&
	but cummit --allow-empty -m "detached cummit" &&
	but reflog expire --all &&
	but prune -n >prune_actual &&
	test_must_be_empty prune_actual
'

test_expect_success 'prune: prune former HEAD after checking out branch' '
	head_oid=$(but rev-parse HEAD) &&
	but checkout --quiet main &&
	but reflog expire --all &&
	but prune -v >prune_actual &&
	grep "$head_oid" prune_actual
'

test_expect_success 'prune: do not prune heads listed as an argument' '
	>file2 &&
	but add file2 &&
	but cummit -m temporary &&
	tmp_head=$(but rev-list -1 HEAD) &&
	but reset HEAD^ &&
	but prune -- $tmp_head &&
	but reset $tmp_head --
'

test_expect_success 'gc --no-prune' '
	add_blob &&
	test-tool chmtime =-$((5001*$day)) $BLOB_FILE &&
	but config gc.pruneExpire 2.days.ago &&
	but gc --no-prune &&
	verbose test 1 = $(but count-objects | sed "s/ .*//") &&
	test_path_is_file $BLOB_FILE
'

test_expect_success 'gc respects gc.pruneExpire' '
	but config gc.pruneExpire 5002.days.ago &&
	but gc &&
	test_path_is_file $BLOB_FILE &&
	but config gc.pruneExpire 5000.days.ago &&
	but gc &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc --prune=<date>' '
	add_blob &&
	test-tool chmtime =-$((5001*$day)) $BLOB_FILE &&
	but gc --prune=5002.days.ago &&
	test_path_is_file $BLOB_FILE &&
	but gc --prune=5000.days.ago &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc --prune=never' '
	add_blob &&
	but gc --prune=never &&
	test_path_is_file $BLOB_FILE &&
	but gc --prune=now &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc respects gc.pruneExpire=never' '
	but config gc.pruneExpire never &&
	add_blob &&
	but gc &&
	test_path_is_file $BLOB_FILE &&
	but config gc.pruneExpire now &&
	but gc &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'prune --expire=never' '
	add_blob &&
	but prune --expire=never &&
	test_path_is_file $BLOB_FILE &&
	but prune &&
	test_path_is_missing $BLOB_FILE
'

test_expect_success 'gc: prune old objects after local clone' '
	add_blob &&
	test-tool chmtime =-$((2*$week+1)) $BLOB_FILE &&
	but clone --no-hardlinks . aclone &&
	(
		cd aclone &&
		verbose test 1 = $(but count-objects | sed "s/ .*//") &&
		test_path_is_file $BLOB_FILE &&
		but gc --prune &&
		verbose test 0 = $(but count-objects | sed "s/ .*//") &&
		test_path_is_missing $BLOB_FILE
	)
'

test_expect_success 'garbage report in count-objects -v' '
	test_when_finished "rm -f .but/objects/pack/fake*" &&
	test_when_finished "rm -f .but/objects/pack/foo*" &&
	>.but/objects/pack/foo &&
	>.but/objects/pack/foo.bar &&
	>.but/objects/pack/foo.keep &&
	>.but/objects/pack/foo.pack &&
	>.but/objects/pack/fake.bar &&
	>.but/objects/pack/fake.keep &&
	>.but/objects/pack/fake.pack &&
	>.but/objects/pack/fake.idx &&
	>.but/objects/pack/fake2.keep &&
	>.but/objects/pack/fake3.idx &&
	but count-objects -v 2>stderr &&
	grep "index file .but/objects/pack/fake.idx is too small" stderr &&
	grep "^warning:" stderr | sort >actual &&
	cat >expected <<\EOF &&
warning: garbage found: .but/objects/pack/fake.bar
warning: garbage found: .but/objects/pack/foo
warning: garbage found: .but/objects/pack/foo.bar
warning: no corresponding .idx or .pack: .but/objects/pack/fake2.keep
warning: no corresponding .idx: .but/objects/pack/foo.keep
warning: no corresponding .idx: .but/objects/pack/foo.pack
warning: no corresponding .pack: .but/objects/pack/fake3.idx
EOF
	test_cmp expected actual
'

test_expect_success 'clean pack garbage with gc' '
	test_when_finished "rm -f .but/objects/pack/fake*" &&
	test_when_finished "rm -f .but/objects/pack/foo*" &&
	>.but/objects/pack/foo.keep &&
	>.but/objects/pack/foo.pack &&
	>.but/objects/pack/fake.idx &&
	>.but/objects/pack/fake2.keep &&
	>.but/objects/pack/fake2.idx &&
	>.but/objects/pack/fake3.keep &&
	but gc &&
	but count-objects -v 2>stderr &&
	grep "^warning:" stderr | sort >actual &&
	cat >expected <<\EOF &&
warning: no corresponding .idx or .pack: .but/objects/pack/fake3.keep
warning: no corresponding .idx: .but/objects/pack/foo.keep
warning: no corresponding .idx: .but/objects/pack/foo.pack
EOF
	test_cmp expected actual
'

test_expect_success 'prune .but/shallow' '
	oid=$(echo hi|but cummit-tree HEAD^{tree}) &&
	echo $oid >.but/shallow &&
	but prune --dry-run >out &&
	grep $oid .but/shallow &&
	grep $oid out &&
	but prune &&
	test_path_is_missing .but/shallow
'

test_expect_success 'prune .but/shallow when there are no loose objects' '
	oid=$(echo hi|but cummit-tree HEAD^{tree}) &&
	echo $oid >.but/shallow &&
	but update-ref refs/heads/shallow-tip $oid &&
	but repack -ad &&
	# verify assumption that all loose objects are gone
	but count-objects | grep ^0 &&
	but prune &&
	echo $oid >expect &&
	test_cmp expect .but/shallow
'

test_expect_success 'prune: handle alternate object database' '
	test_create_repo A &&
	but -C A cummit --allow-empty -m "initial cummit" &&
	but clone --shared A B &&
	but -C B cummit --allow-empty -m "next cummit" &&
	but -C B prune
'

test_expect_success 'prune: handle index in multiple worktrees' '
	but worktree add second-worktree &&
	echo "new blob for second-worktree" >second-worktree/blob &&
	but -C second-worktree add blob &&
	but prune --expire=now &&
	but -C second-worktree show :blob >actual &&
	test_cmp second-worktree/blob actual
'

test_expect_success 'prune: handle HEAD in multiple worktrees' '
	but worktree add --detach third-worktree &&
	echo "new blob for third-worktree" >third-worktree/blob &&
	but -C third-worktree add blob &&
	but -C third-worktree cummit -m "third" &&
	rm .but/worktrees/third-worktree/index &&
	test_must_fail but -C third-worktree show :blob &&
	but prune --expire=now &&
	but -C third-worktree show HEAD:blob >actual &&
	test_cmp third-worktree/blob actual
'

test_expect_success 'prune: handle HEAD reflog in multiple worktrees' '
	but config core.logAllRefUpdates true &&
	echo "lost blob for third-worktree" >expected &&
	(
		cd third-worktree &&
		cat ../expected >blob &&
		but add blob &&
		but cummit -m "second cummit in third" &&
		but clean -f && # Remove untracked left behind by deleting index
		but reset --hard HEAD^
	) &&
	but prune --expire=now &&
	oid=`but hash-object expected` &&
	but -C third-worktree show "$oid" >actual &&
	test_cmp expected actual
'

test_expect_success 'prune: handle expire option correctly' '
	test_must_fail but prune --expire 2>error &&
	test_i18ngrep "requires a value" error &&

	test_must_fail but prune --expire=nyah 2>error &&
	test_i18ngrep "malformed expiration" error &&

	but prune --no-expire
'

test_expect_success 'trivial prune with bitmaps enabled' '
	but repack -adb &&
	blob=$(echo bitmap-unreachable-blob | but hash-object -w --stdin) &&
	but prune --expire=now &&
	but cat-file -e HEAD &&
	test_must_fail but cat-file -e $blob
'

test_expect_success 'old reachable-from-recent retained with bitmaps' '
	but repack -adb &&
	to_drop=$(echo bitmap-from-recent-1 | but hash-object -w --stdin) &&
	test-tool chmtime -86400 .but/objects/$(test_oid_to_path $to_drop) &&
	to_save=$(echo bitmap-from-recent-2 | but hash-object -w --stdin) &&
	test-tool chmtime -86400 .but/objects/$(test_oid_to_path $to_save) &&
	tree=$(printf "100644 blob $to_save\tfile\n" | but mktree) &&
	test-tool chmtime -86400 .but/objects/$(test_oid_to_path $tree) &&
	cummit=$(echo foo | but cummit-tree $tree) &&
	but prune --expire=12.hours.ago &&
	but cat-file -e $cummit &&
	but cat-file -e $tree &&
	but cat-file -e $to_save &&
	test_must_fail but cat-file -e $to_drop
'

test_done
