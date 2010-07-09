#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='git merge

Testing basic merge operations/option parsing.'

. ./test-lib.sh

cat >file <<EOF
1
2
3
4
5
6
7
8
9
EOF

cat >file.1 <<EOF
1 X
2
3
4
5
6
7
8
9
EOF

cat >file.5 <<EOF
1
2
3
4
5 X
6
7
8
9
EOF

cat >file.9 <<EOF
1
2
3
4
5
6
7
8
9 X
EOF

cat  >result.1 <<EOF
1 X
2
3
4
5
6
7
8
9
EOF

cat >result.1-5 <<EOF
1 X
2
3
4
5 X
6
7
8
9
EOF

cat >result.1-5-9 <<EOF
1 X
2
3
4
5 X
6
7
8
9 X
EOF

create_merge_msgs() {
	echo "Merge commit 'c2'" >msg.1-5 &&
	echo "Merge commit 'c2'; commit 'c3'" >msg.1-5-9 &&
	echo "Squashed commit of the following:" >squash.1 &&
	echo >>squash.1 &&
	git log --no-merges ^HEAD c1 >>squash.1 &&
	echo "Squashed commit of the following:" >squash.1-5 &&
	echo >>squash.1-5 &&
	git log --no-merges ^HEAD c2 >>squash.1-5 &&
	echo "Squashed commit of the following:" >squash.1-5-9 &&
	echo >>squash.1-5-9 &&
	git log --no-merges ^HEAD c2 c3 >>squash.1-5-9 &&
	echo > msg.nolog &&
	echo "* commit 'c3':" >msg.log &&
	echo "  commit 3" >>msg.log &&
	echo >>msg.log
}

verify_diff() {
	if ! test_cmp "$1" "$2"
	then
		echo "$3"
		false
	fi
}

verify_merge() {
	verify_diff "$2" "$1" "[OOPS] bad merge result" &&
	if test $(git ls-files -u | wc -l) -gt 0
	then
		echo "[OOPS] unmerged files"
		false
	fi &&
	if test_must_fail git diff --exit-code
	then
		echo "[OOPS] working tree != index"
		false
	fi &&
	if test -n "$3"
	then
		git show -s --pretty=format:%s HEAD >msg.act &&
		verify_diff "$3" msg.act "[OOPS] bad merge message"
	fi
}

verify_head() {
	if test "$1" != "$(git rev-parse HEAD)"
	then
		echo "[OOPS] HEAD != $1"
		false
	fi
}

verify_parents() {
	i=1
	while test $# -gt 0
	do
		if test "$1" != "$(git rev-parse HEAD^$i)"
		then
			echo "[OOPS] HEAD^$i != $1"
			return 1
		fi
		i=$(expr $i + 1)
		shift
	done
}

verify_mergeheads() {
	i=1
	if ! test -f .git/MERGE_HEAD
	then
		echo "[OOPS] MERGE_HEAD is missing"
		false
	fi &&
	while test $# -gt 0
	do
		head=$(head -n $i .git/MERGE_HEAD | sed -ne \$p)
		if test "$1" != "$head"
		then
			echo "[OOPS] MERGE_HEAD $i != $1"
			return 1
		fi
		i=$(expr $i + 1)
		shift
	done
}

verify_no_mergehead() {
	if test -f .git/MERGE_HEAD
	then
		echo "[OOPS] MERGE_HEAD exists"
		false
	fi
}


test_expect_success 'setup' '
	git add file &&
	test_tick &&
	git commit -m "commit 0" &&
	git tag c0 &&
	c0=$(git rev-parse HEAD) &&
	cp file.1 file &&
	git add file &&
	test_tick &&
	git commit -m "commit 1" &&
	git tag c1 &&
	c1=$(git rev-parse HEAD) &&
	git reset --hard "$c0" &&
	cp file.5 file &&
	git add file &&
	test_tick &&
	git commit -m "commit 2" &&
	git tag c2 &&
	c2=$(git rev-parse HEAD) &&
	git reset --hard "$c0" &&
	cp file.9 file &&
	git add file &&
	test_tick &&
	git commit -m "commit 3" &&
	git tag c3 &&
	c3=$(git rev-parse HEAD)
	git reset --hard "$c0" &&
	create_merge_msgs
'

test_debug 'gitk --all'

test_expect_success 'test option parsing' '
	test_must_fail git merge -$ c1 &&
	test_must_fail git merge --no-such c1 &&
	test_must_fail git merge -s foobar c1 &&
	test_must_fail git merge -s=foobar c1 &&
	test_must_fail git merge -m &&
	test_must_fail git merge
'

test_expect_success 'reject non-strategy with a git-merge-foo name' '
	test_must_fail git merge -s index c1
'

test_expect_success 'merge c0 with c1' '
	git reset --hard c0 &&
	git merge c1 &&
	verify_merge file result.1 &&
	verify_head "$c1"
'

test_debug 'gitk --all'

test_expect_success 'merge c0 with c1 with --ff-only' '
	git reset --hard c0 &&
	git merge --ff-only c1 &&
	git merge --ff-only HEAD c0 c1 &&
	verify_merge file result.1 &&
	verify_head "$c1"
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2' '
	git reset --hard c1 &&
	test_tick &&
	git merge c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 and c3' '
	git reset --hard c1 &&
	test_tick &&
	git merge c2 c3 &&
	verify_merge file result.1-5-9 msg.1-5-9 &&
	verify_parents $c1 $c2 $c3
'

test_debug 'gitk --all'

test_expect_success 'failing merges with --ff-only' '
	git reset --hard c1 &&
	test_tick &&
	test_must_fail git merge --ff-only c2 &&
	test_must_fail git merge --ff-only c3 &&
	test_must_fail git merge --ff-only c2 c3
'

test_expect_success 'merge c0 with c1 (no-commit)' '
	git reset --hard c0 &&
	git merge --no-commit c1 &&
	verify_merge file result.1 &&
	verify_head $c1
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 (no-commit)' '
	git reset --hard c1 &&
	git merge --no-commit c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_mergeheads $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 and c3 (no-commit)' '
	git reset --hard c1 &&
	git merge --no-commit c2 c3 &&
	verify_merge file result.1-5-9 &&
	verify_head $c1 &&
	verify_mergeheads $c2 $c3
'

test_debug 'gitk --all'

test_expect_success 'merge c0 with c1 (squash)' '
	git reset --hard c0 &&
	git merge --squash c1 &&
	verify_merge file result.1 &&
	verify_head $c0 &&
	verify_no_mergehead &&
	verify_diff squash.1 .git/SQUASH_MSG "[OOPS] bad squash message"
'

test_debug 'gitk --all'

test_expect_success 'merge c0 with c1 (squash, ff-only)' '
	git reset --hard c0 &&
	git merge --squash --ff-only c1 &&
	verify_merge file result.1 &&
	verify_head $c0 &&
	verify_no_mergehead &&
	verify_diff squash.1 .git/SQUASH_MSG "[OOPS] bad squash message"
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 (squash)' '
	git reset --hard c1 &&
	git merge --squash c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	verify_diff squash.1-5 .git/SQUASH_MSG "[OOPS] bad squash message"
'

test_debug 'gitk --all'

test_expect_success 'unsuccesful merge of c1 with c2 (squash, ff-only)' '
	git reset --hard c1 &&
	test_must_fail git merge --squash --ff-only c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 and c3 (squash)' '
	git reset --hard c1 &&
	git merge --squash c2 c3 &&
	verify_merge file result.1-5-9 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	verify_diff squash.1-5-9 .git/SQUASH_MSG "[OOPS] bad squash message"
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 (no-commit in config)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--no-commit" &&
	git merge c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_mergeheads $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 (squash in config)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--squash" &&
	git merge c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	verify_diff squash.1-5 .git/SQUASH_MSG "[OOPS] bad squash message"
'

test_debug 'gitk --all'

test_expect_success 'override config option -n with --summary' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "-n" &&
	test_tick &&
	git merge --summary c2 >diffstat.txt &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2 &&
	if ! grep "^ file |  *2 +-$" diffstat.txt
	then
		echo "[OOPS] diffstat was not generated with --summary"
		false
	fi
'

test_expect_success 'override config option -n with --stat' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "-n" &&
	test_tick &&
	git merge --stat c2 >diffstat.txt &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2 &&
	if ! grep "^ file |  *2 +-$" diffstat.txt
	then
		echo "[OOPS] diffstat was not generated with --stat"
		false
	fi
'

test_debug 'gitk --all'

test_expect_success 'override config option --stat' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--stat" &&
	test_tick &&
	git merge -n c2 >diffstat.txt &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2 &&
	if grep "^ file |  *2 +-$" diffstat.txt
	then
		echo "[OOPS] diffstat was generated"
		false
	fi
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 (override --no-commit)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--no-commit" &&
	test_tick &&
	git merge --commit c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c2 (override --squash)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--squash" &&
	test_tick &&
	git merge --no-squash c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c0 with c1 (no-ff)' '
	git reset --hard c0 &&
	git config branch.master.mergeoptions "" &&
	test_tick &&
	git merge --no-ff c1 &&
	verify_merge file result.1 &&
	verify_parents $c0 $c1
'

test_debug 'gitk --all'

test_expect_success 'combining --squash and --no-ff is refused' '
	test_must_fail git merge --squash --no-ff c1 &&
	test_must_fail git merge --no-ff --squash c1
'

test_expect_success 'combining --ff-only and --no-ff is refused' '
	test_must_fail git merge --ff-only --no-ff c1 &&
	test_must_fail git merge --no-ff --ff-only c1
'

test_expect_success 'merge c0 with c1 (ff overrides no-ff)' '
	git reset --hard c0 &&
	git config branch.master.mergeoptions "--no-ff" &&
	git merge --ff c1 &&
	verify_merge file result.1 &&
	verify_head $c1
'

test_expect_success 'merge log message' '
	git reset --hard c0 &&
	git merge --no-log c2 &&
	git show -s --pretty=format:%b HEAD >msg.act &&
	verify_diff msg.nolog msg.act "[OOPS] bad merge log message" &&

	git merge --log c3 &&
	git show -s --pretty=format:%b HEAD >msg.act &&
	verify_diff msg.log msg.act "[OOPS] bad merge log message" &&

	git reset --hard HEAD^ &&
	git config merge.log yes &&
	git merge c3 &&
	git show -s --pretty=format:%b HEAD >msg.act &&
	verify_diff msg.log msg.act "[OOPS] bad merge log message"
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c0, c2, c0, and c1' '
       git reset --hard c1 &&
       git config branch.master.mergeoptions "" &&
       test_tick &&
       git merge c0 c2 c0 c1 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c0, c2, c0, and c1' '
       git reset --hard c1 &&
       git config branch.master.mergeoptions "" &&
       test_tick &&
       git merge c0 c2 c0 c1 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'gitk --all'

test_expect_success 'merge c1 with c1 and c2' '
       git reset --hard c1 &&
       git config branch.master.mergeoptions "" &&
       test_tick &&
       git merge c1 c2 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'gitk --all'

test_expect_success 'merge fast-forward in a dirty tree' '
       git reset --hard c0 &&
       mv file file1 &&
       cat file1 >file &&
       rm -f file1 &&
       git merge c2
'

test_debug 'gitk --all'

test_expect_success 'in-index merge' '
	git reset --hard c0 &&
	git merge --no-ff -s resolve c1 > out &&
	grep "Wonderful." out &&
	verify_parents $c0 $c1
'

test_debug 'gitk --all'

test_expect_success 'refresh the index before merging' '
	git reset --hard c1 &&
	cp file file.n && mv -f file.n file &&
	git merge c3
'

cat >expected <<EOF
Merge branch 'c5' (early part)
EOF

test_expect_success 'merge early part of c2' '
	git reset --hard c3 &&
	echo c4 > c4.c &&
	git add c4.c &&
	git commit -m c4 &&
	git tag c4 &&
	echo c5 > c5.c &&
	git add c5.c &&
	git commit -m c5 &&
	git tag c5 &&
	git reset --hard c3 &&
	echo c6 > c6.c &&
	git add c6.c &&
	git commit -m c6 &&
	git tag c6 &&
	git merge c5~1 &&
	git show -s --pretty=format:%s HEAD > actual &&
	test_cmp actual expected
'

test_debug 'gitk --all'

test_expect_success 'merge --no-ff --no-commit && commit' '
	git reset --hard c0 &&
	git merge --no-ff --no-commit c1 &&
	EDITOR=: git commit &&
	verify_parents $c0 $c1
'

test_debug 'gitk --all'

test_expect_success 'amending no-ff merge commit' '
	EDITOR=: git commit --amend &&
	verify_parents $c0 $c1
'

test_debug 'gitk --all'

test_done
