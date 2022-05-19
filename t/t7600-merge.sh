#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='but merge

Testing basic merge operations/option parsing.

! [c0] cummit 0
 ! [c1] cummit 1
  ! [c2] cummit 2
   ! [c3] cummit 3
    ! [c4] c4
     ! [c5] c5
      ! [c6] c6
       * [main] Merge cummit 'c1'
--------
       - [main] Merge cummit 'c1'
 +     * [c1] cummit 1
      +  [c6] c6
     +   [c5] c5
    ++   [c4] c4
   ++++  [c3] cummit 3
  +      [c2] cummit 2
+++++++* [c0] cummit 0
'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

test_write_lines 1 2 3 4 5 6 7 8 9 >file
cp file file.orig
test_write_lines '1 X' 2 3 4 5 6 7 8 9 >file.1
test_write_lines 1 2 '3 X' 4 5 6 7 8 9 >file.3
test_write_lines 1 2 3 4 '5 X' 6 7 8 9 >file.5
test_write_lines 1 2 3 4 5 6 7 8 '9 X' >file.9
test_write_lines 1 2 3 4 5 6 7 8 '9 Y' >file.9y
test_write_lines '1 X' 2 3 4 5 6 7 8 9 >result.1
test_write_lines '1 X' 2 3 4 '5 X' 6 7 8 9 >result.1-5
test_write_lines '1 X' 2 3 4 5 6 7 8 '9 X' >result.1-9
test_write_lines '1 X' 2 3 4 '5 X' 6 7 8 '9 X' >result.1-5-9
test_write_lines '1 X' 2 '3 X' 4 '5 X' 6 7 8 '9 X' >result.1-3-5-9
test_write_lines 1 2 3 4 5 6 7 8 '9 Z' >result.9z

create_merge_msgs () {
	echo "Merge tag 'c2'" >msg.1-5 &&
	echo "Merge tags 'c2' and 'c3'" >msg.1-5-9 &&
	{
		echo "Squashed cummit of the following:" &&
		echo &&
		but log --no-merges ^HEAD c1
	} >squash.1 &&
	{
		echo "Squashed cummit of the following:" &&
		echo &&
		but log --no-merges ^HEAD c2
	} >squash.1-5 &&
	{
		echo "Squashed cummit of the following:" &&
		echo &&
		but log --no-merges ^HEAD c2 c3
	} >squash.1-5-9 &&
	{
		echo "* tag 'c3':" &&
		echo "  cummit 3"
	} >msg.log
}

verify_merge () {
	test_cmp "$2" "$1" &&
	but update-index --refresh &&
	but diff --exit-code &&
	if test -n "$3"
	then
		but show -s --pretty=tformat:%s HEAD >msg.act &&
		test_cmp "$3" msg.act
	fi
}

verify_head () {
	echo "$1" >head.expected &&
	but rev-parse HEAD >head.actual &&
	test_cmp head.expected head.actual
}

verify_parents () {
	test_write_lines "$@" >parents.expected &&
	>parents.actual &&
	i=1 &&
	while test $i -le $#
	do
		but rev-parse HEAD^$i >>parents.actual &&
		i=$(expr $i + 1) ||
		return 1
	done &&
	test_must_fail but rev-parse --verify "HEAD^$i" &&
	test_cmp parents.expected parents.actual
}

verify_mergeheads () {
	test_write_lines "$@" >mergehead.expected &&
	while read sha1 rest
	do
		but rev-parse $sha1
	done <.but/MERGE_HEAD >mergehead.actual &&
	test_cmp mergehead.expected mergehead.actual
}

verify_no_mergehead () {
	! test -e .but/MERGE_HEAD
}

test_expect_success 'setup' '
	but add file &&
	test_tick &&
	but cummit -m "cummit 0" &&
	but tag c0 &&
	c0=$(but rev-parse HEAD) &&
	cp file.1 file &&
	but add file &&
	cp file.1 other &&
	but add other &&
	test_tick &&
	but cummit -m "cummit 1" &&
	but tag c1 &&
	c1=$(but rev-parse HEAD) &&
	but reset --hard "$c0" &&
	cp file.5 file &&
	but add file &&
	test_tick &&
	but cummit -m "cummit 2" &&
	but tag c2 &&
	c2=$(but rev-parse HEAD) &&
	but reset --hard "$c0" &&
	cp file.9y file &&
	but add file &&
	test_tick &&
	but cummit -m "cummit 7" &&
	but tag c7 &&
	but reset --hard "$c0" &&
	cp file.9 file &&
	but add file &&
	test_tick &&
	but cummit -m "cummit 3" &&
	but tag c3 &&
	c3=$(but rev-parse HEAD) &&
	but reset --hard "$c0" &&
	create_merge_msgs
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'test option parsing' '
	test_must_fail but merge -$ c1 &&
	test_must_fail but merge --no-such c1 &&
	test_must_fail but merge -s foobar c1 &&
	test_must_fail but merge -s=foobar c1 &&
	test_must_fail but merge -m &&
	test_must_fail but merge --abort foobar &&
	test_must_fail but merge --abort --quiet &&
	test_must_fail but merge --continue foobar &&
	test_must_fail but merge --continue --quiet &&
	test_must_fail but merge
'

test_expect_success 'merge -h with invalid index' '
	mkdir broken &&
	(
		cd broken &&
		but init &&
		>.but/index &&
		test_expect_code 129 but merge -h 2>usage
	) &&
	test_i18ngrep "[Uu]sage: but merge" broken/usage
'

test_expect_success 'reject non-strategy with a but-merge-foo name' '
	test_must_fail but merge -s index c1
'

test_expect_success 'merge c0 with c1' '
	echo "OBJID HEAD@{0}: merge c1: Fast-forward" >reflog.expected &&

	but reset --hard c0 &&
	but merge c1 &&
	verify_merge file result.1 &&
	verify_head "$c1" &&

	but reflog -1 >reflog.actual &&
	sed "s/$_x05[0-9a-f]*/OBJID/g" reflog.actual >reflog.fuzzy &&
	test_cmp reflog.expected reflog.fuzzy
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 with --ff-only' '
	but reset --hard c0 &&
	but merge --ff-only c1 &&
	but merge --ff-only HEAD c0 c1 &&
	verify_merge file result.1 &&
	verify_head "$c1"
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge from unborn branch' '
	but checkout -f main &&
	test_might_fail but branch -D kid &&

	echo "OBJID HEAD@{0}: initial pull" >reflog.expected &&

	but checkout --orphan kid &&
	test_when_finished "but checkout -f main" &&
	but rm -fr . &&
	test_tick &&
	but merge --ff-only c1 &&
	verify_merge file result.1 &&
	verify_head "$c1" &&

	but reflog -1 >reflog.actual &&
	sed "s/$_x05[0-9a-f][0-9a-f]/OBJID/g" reflog.actual >reflog.fuzzy &&
	test_cmp reflog.expected reflog.fuzzy
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2' '
	but reset --hard c1 &&
	test_tick &&
	but merge c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_expect_success 'merge --squash c3 with c7' '
	but reset --hard c3 &&
	test_must_fail but merge --squash c7 &&
	cat result.9z >file &&
	but cummit --no-edit -a &&

	cat >expect <<-EOF &&
	Squashed cummit of the following:

	$(but show -s c7)

	# Conflicts:
	#	file
	EOF
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'merge c3 with c7 with cummit.cleanup = scissors' '
	but config cummit.cleanup scissors &&
	but reset --hard c3 &&
	test_must_fail but merge c7 &&
	cat result.9z >file &&
	but cummit --no-edit -a &&

	cat >expect <<-\EOF &&
	Merge tag '"'"'c7'"'"'

	# ------------------------ >8 ------------------------
	# Do not modify or remove the line above.
	# Everything below it will be ignored.
	#
	# Conflicts:
	#	file
	EOF
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_expect_success 'merge c3 with c7 with --squash cummit.cleanup = scissors' '
	but config cummit.cleanup scissors &&
	but reset --hard c3 &&
	test_must_fail but merge --squash c7 &&
	cat result.9z >file &&
	but cummit --no-edit -a &&

	cat >expect <<-EOF &&
	Squashed cummit of the following:

	$(but show -s c7)

	# ------------------------ >8 ------------------------
	# Do not modify or remove the line above.
	# Everything below it will be ignored.
	#
	# Conflicts:
	#	file
	EOF
	but cat-file commit HEAD >raw &&
	sed -e "1,/^$/d" raw >actual &&
	test_cmp expect actual
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 and c3' '
	but reset --hard c1 &&
	test_tick &&
	but merge c2 c3 &&
	verify_merge file result.1-5-9 msg.1-5-9 &&
	verify_parents $c1 $c2 $c3
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merges with --ff-only' '
	but reset --hard c1 &&
	test_tick &&
	test_must_fail but merge --ff-only c2 &&
	test_must_fail but merge --ff-only c3 &&
	test_must_fail but merge --ff-only c2 c3 &&
	but reset --hard c0 &&
	but merge c3 &&
	verify_head $c3
'

test_expect_success 'merges with merge.ff=only' '
	but reset --hard c1 &&
	test_tick &&
	test_config merge.ff "only" &&
	test_must_fail but merge c2 &&
	test_must_fail but merge c3 &&
	test_must_fail but merge c2 c3 &&
	but reset --hard c0 &&
	but merge c3 &&
	verify_head $c3
'

test_expect_success 'merge c0 with c1 (no-cummit)' '
	but reset --hard c0 &&
	but merge --no-cummit c1 &&
	verify_merge file result.1 &&
	verify_head $c1
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (no-cummit)' '
	but reset --hard c1 &&
	but merge --no-cummit c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_mergeheads $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 and c3 (no-cummit)' '
	but reset --hard c1 &&
	but merge --no-cummit c2 c3 &&
	verify_merge file result.1-5-9 &&
	verify_head $c1 &&
	verify_mergeheads $c2 $c3
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (squash)' '
	but reset --hard c0 &&
	but merge --squash c1 &&
	verify_merge file result.1 &&
	verify_head $c0 &&
	verify_no_mergehead &&
	test_cmp squash.1 .but/SQUASH_MSG
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (squash, ff-only)' '
	but reset --hard c0 &&
	but merge --squash --ff-only c1 &&
	verify_merge file result.1 &&
	verify_head $c0 &&
	verify_no_mergehead &&
	test_cmp squash.1 .but/SQUASH_MSG
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (squash)' '
	but reset --hard c1 &&
	but merge --squash c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	test_cmp squash.1-5 .but/SQUASH_MSG
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'unsuccessful merge of c1 with c2 (squash, ff-only)' '
	but reset --hard c1 &&
	test_must_fail but merge --squash --ff-only c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 and c3 (squash)' '
	but reset --hard c1 &&
	but merge --squash c2 c3 &&
	verify_merge file result.1-5-9 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	test_cmp squash.1-5-9 .but/SQUASH_MSG
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (no-cummit in config)' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "--no-cummit" &&
	but merge c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_mergeheads $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (log in config)' '
	but reset --hard c1 &&
	but merge --log c2 &&
	but show -s --pretty=tformat:%s%n%b >expect &&

	test_config branch.main.mergeoptions "--log" &&
	but reset --hard c1 &&
	but merge c2 &&
	but show -s --pretty=tformat:%s%n%b >actual &&

	test_cmp expect actual
'

test_expect_success 'merge c1 with c2 (log in config gets overridden)' '
	but reset --hard c1 &&
	but merge c2 &&
	but show -s --pretty=tformat:%s%n%b >expect &&

	test_config branch.main.mergeoptions "--no-log" &&
	test_config merge.log "true" &&
	but reset --hard c1 &&
	but merge c2 &&
	but show -s --pretty=tformat:%s%n%b >actual &&

	test_cmp expect actual
'

test_expect_success 'merge c1 with c2 (squash in config)' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "--squash" &&
	but merge c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	test_cmp squash.1-5 .but/SQUASH_MSG
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'override config option -n with --summary' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "-n" &&
	test_tick &&
	but merge --summary c2 >diffstat.txt &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2 &&
	if ! grep "^ file |  *2 +-$" diffstat.txt
	then
		echo "[OOPS] diffstat was not generated with --summary"
		false
	fi
'

test_expect_success 'override config option -n with --stat' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "-n" &&
	test_tick &&
	but merge --stat c2 >diffstat.txt &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2 &&
	if ! grep "^ file |  *2 +-$" diffstat.txt
	then
		echo "[OOPS] diffstat was not generated with --stat"
		false
	fi
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'override config option --stat' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "--stat" &&
	test_tick &&
	but merge -n c2 >diffstat.txt &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2 &&
	if grep "^ file |  *2 +-$" diffstat.txt
	then
		echo "[OOPS] diffstat was generated"
		false
	fi
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (override --no-cummit)' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "--no-cummit" &&
	test_tick &&
	but merge --cummit c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (override --squash)' '
	but reset --hard c1 &&
	test_config branch.main.mergeoptions "--squash" &&
	test_tick &&
	but merge --no-squash c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (no-ff)' '
	but reset --hard c0 &&
	test_tick &&
	but merge --no-ff c1 &&
	verify_merge file result.1 &&
	verify_parents $c0 $c1
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (merge.ff=false)' '
	but reset --hard c0 &&
	test_config merge.ff "false" &&
	test_tick &&
	but merge c1 &&
	verify_merge file result.1 &&
	verify_parents $c0 $c1
'
test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'combine branch.main.mergeoptions with merge.ff' '
	but reset --hard c0 &&
	test_config branch.main.mergeoptions "--ff" &&
	test_config merge.ff "false" &&
	test_tick &&
	but merge c1 &&
	verify_merge file result.1 &&
	verify_parents "$c0"
'

test_expect_success 'tolerate unknown values for merge.ff' '
	but reset --hard c0 &&
	test_config merge.ff "something-new" &&
	test_tick &&
	but merge c1 2>message &&
	verify_head "$c1" &&
	test_must_be_empty message
'

test_expect_success 'combining --squash and --no-ff is refused' '
	but reset --hard c0 &&
	test_must_fail but merge --squash --no-ff c1 &&
	test_must_fail but merge --no-ff --squash c1
'

test_expect_success 'combining --squash and --cummit is refused' '
	but reset --hard c0 &&
	test_must_fail but merge --squash --cummit c1 &&
	test_must_fail but merge --cummit --squash c1
'

test_expect_success 'option --ff-only overwrites --no-ff' '
	but merge --no-ff --ff-only c1 &&
	test_must_fail but merge --no-ff --ff-only c2
'

test_expect_success 'option --no-ff overrides merge.ff=only config' '
	but reset --hard c0 &&
	test_config merge.ff only &&
	but merge --no-ff c1
'

test_expect_success 'merge c0 with c1 (ff overrides no-ff)' '
	but reset --hard c0 &&
	test_config branch.main.mergeoptions "--no-ff" &&
	but merge --ff c1 &&
	verify_merge file result.1 &&
	verify_head $c1
'

test_expect_success 'merge log message' '
	but reset --hard c0 &&
	but merge --no-log c2 &&
	but show -s --pretty=format:%b HEAD >msg.act &&
	test_must_be_empty msg.act &&

	but reset --hard c0 &&
	test_config branch.main.mergeoptions "--no-ff" &&
	but merge --no-log c2 &&
	but show -s --pretty=format:%b HEAD >msg.act &&
	test_must_be_empty msg.act &&

	but merge --log c3 &&
	but show -s --pretty=format:%b HEAD >msg.act &&
	test_cmp msg.log msg.act &&

	but reset --hard HEAD^ &&
	test_config merge.log "yes" &&
	but merge c3 &&
	but show -s --pretty=format:%b HEAD >msg.act &&
	test_cmp msg.log msg.act
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c0, c2, c0, and c1' '
       but reset --hard c1 &&
       test_tick &&
       but merge c0 c2 c0 c1 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c0, c2, c0, and c1' '
       but reset --hard c1 &&
       test_tick &&
       but merge c0 c2 c0 c1 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c1 and c2' '
       but reset --hard c1 &&
       test_tick &&
       but merge c1 c2 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge fast-forward in a dirty tree' '
       but reset --hard c0 &&
       mv file file1 &&
       cat file1 >file &&
       rm -f file1 &&
       but merge c2
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'in-index merge' '
	but reset --hard c0 &&
	but merge --no-ff -s resolve c1 >out &&
	test_i18ngrep "Wonderful." out &&
	verify_parents $c0 $c1
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'refresh the index before merging' '
	but reset --hard c1 &&
	cp file file.n && mv -f file.n file &&
	but merge c3
'

test_expect_success 'merge with --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but merge --autostash c2 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but show HEAD:file >merge-result &&
	test_cmp result.1-5 merge-result &&
	test_cmp result.1-5-9 file
'

test_expect_success 'merge with merge.autoStash' '
	test_config merge.autoStash true &&
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but merge c2 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but show HEAD:file >merge-result &&
	test_cmp result.1-5 merge-result &&
	test_cmp result.1-5-9 file
'

test_expect_success 'fast-forward merge with --autostash' '
	but reset --hard c0 &&
	but merge-file file file.orig file.5 &&
	but merge --autostash c1 2>err &&
	test_i18ngrep "Applied autostash." err &&
	test_cmp result.1-5 file
'

test_expect_success 'failed fast-forward merge with --autostash' '
	but reset --hard c0 &&
	but merge-file file file.orig file.5 &&
	cp file.5 other &&
	test_when_finished "rm other" &&
	test_must_fail but merge --autostash c1 2>err &&
	test_i18ngrep "Applied autostash." err &&
	test_cmp file.5 file
'

test_expect_success 'octopus merge with --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.3 &&
	but merge --autostash c2 c3 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but show HEAD:file >merge-result &&
	test_cmp result.1-5-9 merge-result &&
	test_cmp result.1-3-5-9 file
'

test_expect_success 'failed merge (exit 2) with --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.5 &&
	test_must_fail but merge -s recursive --autostash c2 c3 2>err &&
	test_i18ngrep "Applied autostash." err &&
	test_cmp result.1-5 file
'

test_expect_success 'conflicted merge with --autostash, --abort restores stash' '
	but reset --hard c3 &&
	cp file.1 file &&
	test_must_fail but merge --autostash c7 &&
	but merge --abort 2>err &&
	test_i18ngrep "Applied autostash." err &&
	test_cmp file.1 file
'

test_expect_success 'completed merge (but cummit) with --no-cummit and --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but diff >expect &&
	but merge --no-cummit --autostash c2 &&
	but stash show -p MERGE_AUTOSTASH >actual &&
	test_cmp expect actual &&
	but cummit 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but show HEAD:file >merge-result &&
	test_cmp result.1-5 merge-result &&
	test_cmp result.1-5-9 file
'

test_expect_success 'completed merge (but merge --continue) with --no-cummit and --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but diff >expect &&
	but merge --no-cummit --autostash c2 &&
	but stash show -p MERGE_AUTOSTASH >actual &&
	test_cmp expect actual &&
	but merge --continue 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but show HEAD:file >merge-result &&
	test_cmp result.1-5 merge-result &&
	test_cmp result.1-5-9 file
'

test_expect_success 'aborted merge (merge --abort) with --no-cummit and --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but diff >expect &&
	but merge --no-cummit --autostash c2 &&
	but stash show -p MERGE_AUTOSTASH >actual &&
	test_cmp expect actual &&
	but merge --abort 2>err &&
	test_i18ngrep "Applied autostash." err &&
	but diff >actual &&
	test_cmp expect actual
'

test_expect_success 'aborted merge (reset --hard) with --no-cummit and --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but diff >expect &&
	but merge --no-cummit --autostash c2 &&
	but stash show -p MERGE_AUTOSTASH >actual &&
	test_cmp expect actual &&
	but reset --hard 2>err &&
	test_i18ngrep "Autostash exists; creating a new stash entry." err &&
	but diff --exit-code
'

test_expect_success 'quit merge with --no-cummit and --autostash' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9 &&
	but diff >expect &&
	but merge --no-cummit --autostash c2 &&
	but stash show -p MERGE_AUTOSTASH >actual &&
	test_cmp expect actual &&
	but diff HEAD >expect &&
	but merge --quit 2>err &&
	test_i18ngrep "Autostash exists; creating a new stash entry." err &&
	but diff HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'merge with conflicted --autostash changes' '
	but reset --hard c1 &&
	but merge-file file file.orig file.9y &&
	but diff >expect &&
	test_when_finished "test_might_fail but stash drop" &&
	but merge --autostash c3 2>err &&
	test_i18ngrep "Applying autostash resulted in conflicts." err &&
	but show HEAD:file >merge-result &&
	test_cmp result.1-9 merge-result &&
	but stash show -p >actual &&
	test_cmp expect actual
'

cat >expected.branch <<\EOF
Merge branch 'c5-branch' (early part)
EOF
cat >expected.tag <<\EOF
Merge cummit 'c5~1'
EOF

test_expect_success 'merge early part of c2' '
	but reset --hard c3 &&
	echo c4 >c4.c &&
	but add c4.c &&
	but cummit -m c4 &&
	but tag c4 &&
	echo c5 >c5.c &&
	but add c5.c &&
	but cummit -m c5 &&
	but tag c5 &&
	but reset --hard c3 &&
	echo c6 >c6.c &&
	but add c6.c &&
	but cummit -m c6 &&
	but tag c6 &&
	but branch -f c5-branch c5 &&
	but merge c5-branch~1 &&
	but show -s --pretty=tformat:%s HEAD >actual.branch &&
	but reset --keep HEAD^ &&
	but merge c5~1 &&
	but show -s --pretty=tformat:%s HEAD >actual.tag &&
	test_cmp expected.branch actual.branch &&
	test_cmp expected.tag actual.tag
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'merge --no-ff --no-cummit && cummit' '
	but reset --hard c0 &&
	but merge --no-ff --no-cummit c1 &&
	EDITOR=: but cummit &&
	verify_parents $c0 $c1
'

test_debug 'but log --graph --decorate --oneline --all'

test_expect_success 'amending no-ff merge cummit' '
	EDITOR=: but cummit --amend &&
	verify_parents $c0 $c1
'

test_debug 'but log --graph --decorate --oneline --all'

cat >editor <<\EOF
#!/bin/sh
# Add a new message string that was not in the template
(
	echo "Merge work done on the side branch c1"
	echo
	cat "$1"
) >"$1.tmp" && mv "$1.tmp" "$1"
# strip comments and blank lines from end of message
sed -e '/^#/d' "$1" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' >expected
EOF
chmod 755 editor

test_expect_success 'merge --no-ff --edit' '
	but reset --hard c0 &&
	EDITOR=./editor but merge --no-ff --edit c1 &&
	verify_parents $c0 $c1 &&
	but cat-file commit HEAD >raw &&
	grep "work done on the side branch" raw &&
	sed "1,/^$/d" >actual raw &&
	test_cmp expected actual
'

test_expect_success 'merge annotated/signed tag w/o tracking' '
	test_when_finished "rm -rf dst; but tag -d anno1" &&
	but tag -a -m "anno c1" anno1 c1 &&
	but init dst &&
	but rev-parse c1 >dst/expect &&
	(
		# c0 fast-forwards to c1 but because this repository
		# is not a "downstream" whose refs/tags follows along
		# tag from the "upstream", this pull defaults to --no-ff
		cd dst &&
		but pull .. c0 &&
		but pull .. anno1 &&
		but rev-parse HEAD^2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'merge annotated/signed tag w/ tracking' '
	test_when_finished "rm -rf dst; but tag -d anno1" &&
	but tag -a -m "anno c1" anno1 c1 &&
	but init dst &&
	but rev-parse c1 >dst/expect &&
	(
		# c0 fast-forwards to c1 and because this repository
		# is a "downstream" whose refs/tags follows along
		# tag from the "upstream", this pull defaults to --ff
		cd dst &&
		but remote add origin .. &&
		but pull origin c0 &&
		but fetch origin &&
		but merge anno1 &&
		but rev-parse HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success GPG 'merge --ff-only tag' '
	but reset --hard c0 &&
	but cummit --allow-empty -m "A newer cummit" &&
	but tag -s -m "A newer cummit" signed &&
	but reset --hard c0 &&

	but merge --ff-only signed &&
	but rev-parse signed^0 >expect &&
	but rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'merge --no-edit tag should skip editor' '
	but reset --hard c0 &&
	but cummit --allow-empty -m "A newer cummit" &&
	but tag -f -s -m "A newer cummit" signed &&
	but reset --hard c0 &&

	EDITOR=false but merge --no-edit --no-ff signed &&
	but rev-parse signed^0 >expect &&
	but rev-parse HEAD^2 >actual &&
	test_cmp expect actual
'

test_expect_success 'set up mod-256 conflict scenario' '
	# 256 near-identical stanzas...
	for i in $(test_seq 1 256); do
		for j in 1 2 3 4 5; do
			echo $i-$j || return 1
		done
	done >file &&
	but add file &&
	but cummit -m base &&

	# one side changes the first line of each to "main"
	sed s/-1/-main/ file >tmp &&
	mv tmp file &&
	but cummit -am main &&

	# and the other to "side"; merging the two will
	# yield 256 separate conflicts
	but checkout -b side HEAD^ &&
	sed s/-1/-side/ file >tmp &&
	mv tmp file &&
	but cummit -am side
'

test_expect_success 'merge detects mod-256 conflicts (recursive)' '
	but reset --hard &&
	test_must_fail but merge -s recursive main
'

test_expect_success 'merge detects mod-256 conflicts (resolve)' '
	but reset --hard &&
	test_must_fail but merge -s resolve main
'

test_expect_success 'merge nothing into void' '
	but init void &&
	(
		cd void &&
		but remote add up .. &&
		but fetch up &&
		test_must_fail but merge FETCH_HEAD
	)
'

test_expect_success 'merge can be completed with --continue' '
	but reset --hard c0 &&
	but merge --no-ff --no-cummit c1 &&
	but merge --continue &&
	verify_parents $c0 $c1
'

write_script .but/FAKE_EDITOR <<EOF
# kill -TERM command added below.
EOF

test_expect_success EXECKEEPSPID 'killed merge can be completed with --continue' '
	but reset --hard c0 &&
	! "$SHELL_PATH" -c '\''
	  echo kill -TERM $$ >>.but/FAKE_EDITOR
	  GIT_EDITOR=.but/FAKE_EDITOR
	  export GIT_EDITOR
	  exec but merge --no-ff --edit c1'\'' &&
	but merge --continue &&
	verify_parents $c0 $c1
'

test_expect_success 'merge --quit' '
	but init merge-quit &&
	(
		cd merge-quit &&
		test_cummit base &&
		echo one >>base.t &&
		but cummit -am one &&
		but branch one &&
		but checkout base &&
		echo two >>base.t &&
		but cummit -am two &&
		test_must_fail but -c rerere.enabled=true merge one &&
		test_path_is_file .but/MERGE_HEAD &&
		test_path_is_file .but/MERGE_MODE &&
		test_path_is_file .but/MERGE_MSG &&
		but rerere status >rerere.before &&
		but merge --quit &&
		test_path_is_missing .but/MERGE_HEAD &&
		test_path_is_missing .but/MERGE_MODE &&
		test_path_is_missing .but/MERGE_MSG &&
		but rerere status >rerere.after &&
		test_must_be_empty rerere.after &&
		! test_cmp rerere.after rerere.before
	)
'

test_expect_success 'merge suggests matching remote refname' '
	but cummit --allow-empty -m not-local &&
	but update-ref refs/remotes/origin/not-local HEAD &&
	but reset --hard HEAD^ &&

	# This is white-box testing hackery; we happen to know
	# that reading packed refs is more picky about the memory
	# ownership of strings we pass to for_each_ref() callbacks.
	but pack-refs --all --prune &&

	test_must_fail but merge not-local 2>stderr &&
	grep origin/not-local stderr
'

test_expect_success 'suggested names are not ambiguous' '
	but update-ref refs/heads/origin/not-local HEAD &&
	test_must_fail but merge not-local 2>stderr &&
	grep remotes/origin/not-local stderr
'

test_done
