#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='git merge

Testing basic merge operations/option parsing.

! [c0] commit 0
 ! [c1] commit 1
  ! [c2] commit 2
   ! [c3] commit 3
    ! [c4] c4
     ! [c5] c5
      ! [c6] c6
       * [master] Merge commit 'c1'
--------
       - [master] Merge commit 'c1'
 +     * [c1] commit 1
      +  [c6] c6
     +   [c5] c5
    ++   [c4] c4
   ++++  [c3] commit 3
  +      [c2] commit 2
+++++++* [c0] commit 0
'

. ./test-lib.sh

printf '%s\n' 1 2 3 4 5 6 7 8 9 >file
printf '%s\n' '1 X' 2 3 4 5 6 7 8 9 >file.1
printf '%s\n' 1 2 3 4 '5 X' 6 7 8 9 >file.5
printf '%s\n' 1 2 3 4 5 6 7 8 '9 X' >file.9
printf '%s\n' '1 X' 2 3 4 5 6 7 8 9 >result.1
printf '%s\n' '1 X' 2 3 4 '5 X' 6 7 8 9 >result.1-5
printf '%s\n' '1 X' 2 3 4 '5 X' 6 7 8 '9 X' >result.1-5-9
>empty

create_merge_msgs () {
	echo "Merge commit 'c2'" >msg.1-5 &&
	echo "Merge commit 'c2'; commit 'c3'" >msg.1-5-9 &&
	{
		echo "Squashed commit of the following:" &&
		echo &&
		git log --no-merges ^HEAD c1
	} >squash.1 &&
	{
		echo "Squashed commit of the following:" &&
		echo &&
		git log --no-merges ^HEAD c2
	} >squash.1-5 &&
	{
		echo "Squashed commit of the following:" &&
		echo &&
		git log --no-merges ^HEAD c2 c3
	} >squash.1-5-9 &&
	echo >msg.nolog &&
	{
		echo "* commit 'c3':" &&
		echo "  commit 3" &&
		echo
	} >msg.log
}

verify_merge () {
	test_cmp "$2" "$1" &&
	git update-index --refresh &&
	git diff --exit-code &&
	if test -n "$3"
	then
		git show -s --pretty=format:%s HEAD >msg.act &&
		test_cmp "$3" msg.act
	fi
}

verify_head () {
	echo "$1" >head.expected &&
	git rev-parse HEAD >head.actual &&
	test_cmp head.expected head.actual
}

verify_parents () {
	printf '%s\n' "$@" >parents.expected &&
	>parents.actual &&
	i=1 &&
	while test $i -le $#
	do
		git rev-parse HEAD^$i >>parents.actual &&
		i=$(expr $i + 1) ||
		return 1
	done &&
	test_must_fail git rev-parse --verify "HEAD^$i" &&
	test_cmp parents.expected parents.actual
}

verify_mergeheads () {
	printf '%s\n' "$@" >mergehead.expected &&
	test_cmp mergehead.expected .git/MERGE_HEAD
}

verify_no_mergehead () {
	! test -e .git/MERGE_HEAD
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

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'test option parsing' '
	test_must_fail git merge -$ c1 &&
	test_must_fail git merge --no-such c1 &&
	test_must_fail git merge -s foobar c1 &&
	test_must_fail git merge -s=foobar c1 &&
	test_must_fail git merge -m &&
	test_must_fail git merge
'

test_expect_success 'merge -h with invalid index' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git merge -h 2>usage
	) &&
	grep "[Uu]sage: git merge" broken/usage
'

test_expect_success 'reject non-strategy with a git-merge-foo name' '
	test_must_fail git merge -s index c1
'

test_expect_success 'merge c0 with c1' '
	echo "OBJID HEAD@{0}: merge c1: Fast-forward" >reflog.expected &&

	git reset --hard c0 &&
	git merge c1 &&
	verify_merge file result.1 &&
	verify_head "$c1" &&

	git reflog -1 >reflog.actual &&
	sed "s/$_x05[0-9a-f]*/OBJID/g" reflog.actual >reflog.fuzzy &&
	test_cmp reflog.expected reflog.fuzzy
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 with --ff-only' '
	git reset --hard c0 &&
	git merge --ff-only c1 &&
	git merge --ff-only HEAD c0 c1 &&
	verify_merge file result.1 &&
	verify_head "$c1"
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge from unborn branch' '
	git checkout -f master &&
	test_might_fail git branch -D kid &&

	echo "OBJID HEAD@{0}: initial pull" >reflog.expected &&

	git checkout --orphan kid &&
	test_when_finished "git checkout -f master" &&
	git rm -fr . &&
	test_tick &&
	git merge --ff-only c1 &&
	verify_merge file result.1 &&
	verify_head "$c1" &&

	git reflog -1 >reflog.actual &&
	sed "s/$_x05[0-9a-f][0-9a-f]/OBJID/g" reflog.actual >reflog.fuzzy &&
	test_cmp reflog.expected reflog.fuzzy
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2' '
	git reset --hard c1 &&
	test_tick &&
	git merge c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 and c3' '
	git reset --hard c1 &&
	test_tick &&
	git merge c2 c3 &&
	verify_merge file result.1-5-9 msg.1-5-9 &&
	verify_parents $c1 $c2 $c3
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merges with --ff-only' '
	git reset --hard c1 &&
	test_tick &&
	test_must_fail git merge --ff-only c2 &&
	test_must_fail git merge --ff-only c3 &&
	test_must_fail git merge --ff-only c2 c3 &&
	git reset --hard c0 &&
	git merge c3 &&
	verify_head $c3
'

test_expect_success 'merges with merge.ff=only' '
	git reset --hard c1 &&
	test_tick &&
	test_when_finished "git config --unset merge.ff" &&
	git config merge.ff only &&
	test_must_fail git merge c2 &&
	test_must_fail git merge c3 &&
	test_must_fail git merge c2 c3 &&
	git reset --hard c0 &&
	git merge c3 &&
	verify_head $c3
'

test_expect_success 'merge c0 with c1 (no-commit)' '
	git reset --hard c0 &&
	git merge --no-commit c1 &&
	verify_merge file result.1 &&
	verify_head $c1
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (no-commit)' '
	git reset --hard c1 &&
	git merge --no-commit c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_mergeheads $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 and c3 (no-commit)' '
	git reset --hard c1 &&
	git merge --no-commit c2 c3 &&
	verify_merge file result.1-5-9 &&
	verify_head $c1 &&
	verify_mergeheads $c2 $c3
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (squash)' '
	git reset --hard c0 &&
	git merge --squash c1 &&
	verify_merge file result.1 &&
	verify_head $c0 &&
	verify_no_mergehead &&
	test_cmp squash.1 .git/SQUASH_MSG
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (squash, ff-only)' '
	git reset --hard c0 &&
	git merge --squash --ff-only c1 &&
	verify_merge file result.1 &&
	verify_head $c0 &&
	verify_no_mergehead &&
	test_cmp squash.1 .git/SQUASH_MSG
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (squash)' '
	git reset --hard c1 &&
	git merge --squash c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	test_cmp squash.1-5 .git/SQUASH_MSG
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'unsuccesful merge of c1 with c2 (squash, ff-only)' '
	git reset --hard c1 &&
	test_must_fail git merge --squash --ff-only c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 and c3 (squash)' '
	git reset --hard c1 &&
	git merge --squash c2 c3 &&
	verify_merge file result.1-5-9 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	test_cmp squash.1-5-9 .git/SQUASH_MSG
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (no-commit in config)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--no-commit" &&
	git merge c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_mergeheads $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (log in config)' '
	git config branch.master.mergeoptions "" &&
	git reset --hard c1 &&
	git merge --log c2 &&
	git show -s --pretty=tformat:%s%n%b >expect &&

	git config branch.master.mergeoptions --log &&
	git reset --hard c1 &&
	git merge c2 &&
	git show -s --pretty=tformat:%s%n%b >actual &&

	test_cmp expect actual
'

test_expect_success 'merge c1 with c2 (log in config gets overridden)' '
	test_when_finished "git config --remove-section branch.master" &&
	test_when_finished "git config --remove-section merge" &&
	test_might_fail git config --remove-section branch.master &&
	test_might_fail git config --remove-section merge &&

	git reset --hard c1 &&
	git merge c2 &&
	git show -s --pretty=tformat:%s%n%b >expect &&

	git config branch.master.mergeoptions "--no-log" &&
	git config merge.log true &&
	git reset --hard c1 &&
	git merge c2 &&
	git show -s --pretty=tformat:%s%n%b >actual &&

	test_cmp expect actual
'

test_expect_success 'merge c1 with c2 (squash in config)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--squash" &&
	git merge c2 &&
	verify_merge file result.1-5 &&
	verify_head $c1 &&
	verify_no_mergehead &&
	test_cmp squash.1-5 .git/SQUASH_MSG
'

test_debug 'git log --graph --decorate --oneline --all'

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

test_debug 'git log --graph --decorate --oneline --all'

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

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (override --no-commit)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--no-commit" &&
	test_tick &&
	git merge --commit c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c2 (override --squash)' '
	git reset --hard c1 &&
	git config branch.master.mergeoptions "--squash" &&
	test_tick &&
	git merge --no-squash c2 &&
	verify_merge file result.1-5 msg.1-5 &&
	verify_parents $c1 $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (no-ff)' '
	git reset --hard c0 &&
	git config branch.master.mergeoptions "" &&
	test_tick &&
	git merge --no-ff c1 &&
	verify_merge file result.1 &&
	verify_parents $c0 $c1
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c0 with c1 (merge.ff=false)' '
	git reset --hard c0 &&
	git config merge.ff false &&
	test_tick &&
	git merge c1 &&
	git config --remove-section merge &&
	verify_merge file result.1 &&
	verify_parents $c0 $c1
'
test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'combine branch.master.mergeoptions with merge.ff' '
	git reset --hard c0 &&
	git config branch.master.mergeoptions --ff &&
	git config merge.ff false &&
	test_tick &&
	git merge c1 &&
	git config --remove-section "branch.master" &&
	git config --remove-section "merge" &&
	verify_merge file result.1 &&
	verify_parents "$c0"
'

test_expect_success 'tolerate unknown values for merge.ff' '
	git reset --hard c0 &&
	git config merge.ff something-new &&
	test_tick &&
	git merge c1 2>message &&
	git config --remove-section "merge" &&
	verify_head "$c1" &&
	test_cmp empty message
'

test_expect_success 'combining --squash and --no-ff is refused' '
	git reset --hard c0 &&
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
	test_cmp msg.nolog msg.act &&

	git merge --log c3 &&
	git show -s --pretty=format:%b HEAD >msg.act &&
	test_cmp msg.log msg.act &&

	git reset --hard HEAD^ &&
	git config merge.log yes &&
	git merge c3 &&
	git show -s --pretty=format:%b HEAD >msg.act &&
	test_cmp msg.log msg.act
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c0, c2, c0, and c1' '
       git reset --hard c1 &&
       git config branch.master.mergeoptions "" &&
       test_tick &&
       git merge c0 c2 c0 c1 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c0, c2, c0, and c1' '
       git reset --hard c1 &&
       git config branch.master.mergeoptions "" &&
       test_tick &&
       git merge c0 c2 c0 c1 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge c1 with c1 and c2' '
       git reset --hard c1 &&
       git config branch.master.mergeoptions "" &&
       test_tick &&
       git merge c1 c2 &&
       verify_merge file result.1-5 &&
       verify_parents $c1 $c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge fast-forward in a dirty tree' '
       git reset --hard c0 &&
       mv file file1 &&
       cat file1 >file &&
       rm -f file1 &&
       git merge c2
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'in-index merge' '
	git reset --hard c0 &&
	git merge --no-ff -s resolve c1 >out &&
	test_i18ngrep "Wonderful." out &&
	verify_parents $c0 $c1
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'refresh the index before merging' '
	git reset --hard c1 &&
	cp file file.n && mv -f file.n file &&
	git merge c3
'

cat >expected.branch <<\EOF
Merge branch 'c5-branch' (early part)
EOF
cat >expected.tag <<\EOF
Merge commit 'c5~1'
EOF

test_expect_success 'merge early part of c2' '
	git reset --hard c3 &&
	echo c4 >c4.c &&
	git add c4.c &&
	git commit -m c4 &&
	git tag c4 &&
	echo c5 >c5.c &&
	git add c5.c &&
	git commit -m c5 &&
	git tag c5 &&
	git reset --hard c3 &&
	echo c6 >c6.c &&
	git add c6.c &&
	git commit -m c6 &&
	git tag c6 &&
	git branch -f c5-branch c5 &&
	git merge c5-branch~1 &&
	git show -s --pretty=format:%s HEAD >actual.branch &&
	git reset --keep HEAD^ &&
	git merge c5~1 &&
	git show -s --pretty=format:%s HEAD >actual.tag &&
	test_cmp expected.branch actual.branch &&
	test_cmp expected.tag actual.tag
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'merge --no-ff --no-commit && commit' '
	git reset --hard c0 &&
	git merge --no-ff --no-commit c1 &&
	EDITOR=: git commit &&
	verify_parents $c0 $c1
'

test_debug 'git log --graph --decorate --oneline --all'

test_expect_success 'amending no-ff merge commit' '
	EDITOR=: git commit --amend &&
	verify_parents $c0 $c1
'

test_debug 'git log --graph --decorate --oneline --all'

cat >editor <<\EOF
#!/bin/sh
# Add a new message string that was not in the template
(
	echo "Merge work done on the side branch c1"
	echo
	cat <"$1"
) >"$1.tmp" && mv "$1.tmp" "$1"
# strip comments and blank lines from end of message
sed -e '/^#/d' < "$1" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' > expected
EOF
chmod 755 editor

test_expect_success 'merge --no-ff --edit' '
	git reset --hard c0 &&
	EDITOR=./editor git merge --no-ff --edit c1 &&
	verify_parents $c0 $c1 &&
	git cat-file commit HEAD >raw &&
	grep "work done on the side branch" raw &&
	sed "1,/^$/d" >actual raw &&
	test_cmp actual expected
'

test_done
