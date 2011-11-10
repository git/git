#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git checkout tests.

Creates master, forks renamer and side branches from it.
Test switching across them.

  ! [master] Initial A one, A two
   * [renamer] Renamer R one->uno, M two
    ! [side] Side M one, D two, A three
  ---
    + [side] Side M one, D two, A three
   *  [renamer] Renamer R one->uno, M two
  +*+ [master] Initial A one, A two

'

. ./test-lib.sh

test_tick

fill () {
	for i
	do
		echo "$i"
	done
}


test_expect_success setup '

	fill x y z > same &&
	fill 1 2 3 4 5 6 7 8 >one &&
	fill a b c d e >two &&
	git add same one two &&
	git commit -m "Initial A one, A two" &&

	git checkout -b renamer &&
	rm -f one &&
	fill 1 3 4 5 6 7 8 >uno &&
	git add uno &&
	fill a b c d e f >two &&
	git commit -a -m "Renamer R one->uno, M two" &&

	git checkout -b side master &&
	fill 1 2 3 4 5 6 7 >one &&
	fill A B C D E >three &&
	rm -f two &&
	git update-index --add --remove one two three &&
	git commit -m "Side M one, D two, A three" &&

	git checkout master
'

test_expect_success "checkout from non-existing branch" '

	git checkout -b delete-me master &&
	rm .git/refs/heads/delete-me &&
	test refs/heads/delete-me = "$(git symbolic-ref HEAD)" &&
	git checkout master &&
	test refs/heads/master = "$(git symbolic-ref HEAD)"
'

test_expect_success "checkout with dirty tree without -m" '

	fill 0 1 2 3 4 5 6 7 8 >one &&
	if git checkout side
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi

'

test_expect_success "checkout with unrelated dirty tree without -m" '

	git checkout -f master &&
	fill 0 1 2 3 4 5 6 7 8 >same &&
	cp same kept
	git checkout side >messages &&
	test_cmp same kept
	(cat > messages.expect <<EOF
M	same
EOF
) &&
	touch messages.expect &&
	test_cmp messages.expect messages
'

test_expect_success "checkout -m with dirty tree" '

	git checkout -f master &&
	git clean -f &&

	fill 0 1 2 3 4 5 6 7 8 >one &&
	git checkout -m side > messages &&

	test "$(git symbolic-ref HEAD)" = "refs/heads/side" &&

	(cat >expect.messages <<EOF
M	one
EOF
) &&
	test_cmp expect.messages messages &&

	fill "M	one" "A	three" "D	two" >expect.master &&
	git diff --name-status master >current.master &&
	test_cmp expect.master current.master &&

	fill "M	one" >expect.side &&
	git diff --name-status side >current.side &&
	test_cmp expect.side current.side &&

	: >expect.index &&
	git diff --cached >current.index &&
	test_cmp expect.index current.index
'

test_expect_success "checkout -m with dirty tree, renamed" '

	git checkout -f master && git clean -f &&

	fill 1 2 3 4 5 7 8 >one &&
	if git checkout renamer
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi &&

	git checkout -m renamer &&
	fill 1 3 4 5 7 8 >expect &&
	test_cmp expect uno &&
	! test -f one &&
	git diff --cached >current &&
	! test -s current

'

test_expect_success 'checkout -m with merge conflict' '

	git checkout -f master && git clean -f &&

	fill 1 T 3 4 5 6 S 8 >one &&
	if git checkout renamer
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi &&

	git checkout -m renamer &&

	git diff master:one :3:uno |
	sed -e "1,/^@@/d" -e "/^ /d" -e "s/^-/d/" -e "s/^+/a/" >current &&
	fill d2 aT d7 aS >expect &&
	test_cmp current expect &&
	git diff --cached two >current &&
	! test -s current
'

test_expect_success 'checkout to detach HEAD' '

	git checkout -f renamer && git clean -f &&
	git checkout renamer^ 2>messages &&
	(cat >messages.expect <<EOF
Note: moving to '\''renamer^'\'' which isn'\''t a local branch
If you want to create a new branch from this checkout, you may do so
(now or later) by using -b with the checkout command again. Example:
  git checkout -b <new_branch_name>
HEAD is now at 7329388... Initial A one, A two
EOF
) &&
	test_cmp messages.expect messages &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/master) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with branchname^' '

	git checkout -f master && git clean -f &&
	git checkout renamer^ &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/master) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with :/message' '

	git checkout -f master && git clean -f &&
	git checkout ":/Initial" &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/master) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with HEAD^0' '

	git checkout -f master && git clean -f &&
	git checkout HEAD^0 &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/master) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout with ambiguous tag/branch names' '

	git tag both side &&
	git branch both master &&
	git reset --hard &&
	git checkout master &&

	git checkout both &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/master) &&
	test "z$H" = "z$M" &&
	name=$(git symbolic-ref HEAD 2>/dev/null) &&
	test "z$name" = zrefs/heads/both

'

test_expect_success 'checkout with ambiguous tag/branch names' '

	git reset --hard &&
	git checkout master &&

	git tag frotz side &&
	git branch frotz master &&
	git reset --hard &&
	git checkout master &&

	git checkout tags/frotz &&
	H=$(git rev-parse --verify HEAD) &&
	S=$(git show-ref -s --verify refs/heads/side) &&
	test "z$H" = "z$S" &&
	if name=$(git symbolic-ref HEAD 2>/dev/null)
	then
		echo "Bad -- should have detached"
		false
	else
		: happy
	fi

'

test_expect_success 'switch branches while in subdirectory' '

	git reset --hard &&
	git checkout master &&

	mkdir subs &&
	(
		cd subs &&
		git checkout side
	) &&
	! test -f subs/one &&
	rm -fr subs

'

test_expect_success 'checkout specific path while in subdirectory' '

	git reset --hard &&
	git checkout side &&
	mkdir subs &&
	>subs/bero &&
	git add subs/bero &&
	git commit -m "add subs/bero" &&

	git checkout master &&
	mkdir -p subs &&
	(
		cd subs &&
		git checkout side -- bero
	) &&
	test -f subs/bero

'

test_expect_success \
    'checkout w/--track sets up tracking' '
    git config branch.autosetupmerge false &&
    git checkout master &&
    git checkout --track -b track1 &&
    test "$(git config branch.track1.remote)" &&
    test "$(git config branch.track1.merge)"'

test_expect_success \
    'checkout w/autosetupmerge=always sets up tracking' '
    git config branch.autosetupmerge always &&
    git checkout master &&
    git checkout -b track2 &&
    test "$(git config branch.track2.remote)" &&
    test "$(git config branch.track2.merge)"
    git config branch.autosetupmerge false'

test_expect_success 'checkout w/--track from non-branch HEAD fails' '
    git checkout master^0 &&
    test_must_fail git symbolic-ref HEAD &&
    test_must_fail git checkout --track -b track &&
    test_must_fail git rev-parse --verify track &&
    test_must_fail git symbolic-ref HEAD &&
    test "z$(git rev-parse master^0)" = "z$(git rev-parse HEAD)"
'

test_expect_success 'detach a symbolic link HEAD' '
    git checkout master &&
    git config --bool core.prefersymlinkrefs yes &&
    git checkout side &&
    git checkout master &&
    it=$(git symbolic-ref HEAD) &&
    test "z$it" = zrefs/heads/master &&
    here=$(git rev-parse --verify refs/heads/master) &&
    git checkout side^ &&
    test "z$(git rev-parse --verify refs/heads/master)" = "z$here"
'

test_expect_success \
    'checkout with --track fakes a sensible -b <name>' '
    git update-ref refs/remotes/origin/koala/bear renamer &&
    git update-ref refs/new/koala/bear renamer &&

    git checkout --track origin/koala/bear &&
    test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)" &&

    git checkout master && git branch -D koala/bear &&

    git checkout --track refs/remotes/origin/koala/bear &&
    test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)" &&

    git checkout master && git branch -D koala/bear &&

    git checkout --track remotes/origin/koala/bear &&
    test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)" &&

    git checkout master && git branch -D koala/bear &&

    git checkout --track refs/new/koala/bear &&
    test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
    test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)"
'

test_expect_success \
    'checkout with --track, but without -b, fails with too short tracked name' '
    test_must_fail git checkout --track renamer'

setup_conflicting_index () {
	rm -f .git/index &&
	O=$(echo original | git hash-object -w --stdin) &&
	A=$(echo ourside | git hash-object -w --stdin) &&
	B=$(echo theirside | git hash-object -w --stdin) &&
	(
		echo "100644 $A 0	fild" &&
		echo "100644 $O 1	file" &&
		echo "100644 $A 2	file" &&
		echo "100644 $B 3	file" &&
		echo "100644 $A 0	filf"
	) | git update-index --index-info
}

test_expect_success 'checkout an unmerged path should fail' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	test_must_fail git checkout fild file filf &&
	test_cmp sample fild &&
	test_cmp sample filf &&
	test_cmp sample file
'

test_expect_success 'checkout with an unmerged path can be ignored' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout -f fild file filf &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp sample file
'

test_expect_success 'checkout unmerged stage' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout --ours . &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp expect file &&
	git checkout --theirs file &&
	test ztheirside = "z$(cat file)"
'

test_expect_success 'checkout with --merge' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout -m -- fild file filf &&
	(
		echo "<<<<<<< ours"
		echo ourside
		echo "======="
		echo theirside
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout with --merge, in diff3 -m style' '
	git config merge.conflictstyle diff3 &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout -m -- fild file filf &&
	(
		echo "<<<<<<< ours"
		echo ourside
		echo "|||||||"
		echo original
		echo "======="
		echo theirside
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout --conflict=merge, overriding config' '
	git config merge.conflictstyle diff3 &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout --conflict=merge -- fild file filf &&
	(
		echo "<<<<<<< ours"
		echo ourside
		echo "======="
		echo theirside
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout --conflict=diff3' '
	git config --unset merge.conflictstyle
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout --conflict=diff3 -- fild file filf &&
	(
		echo "<<<<<<< ours"
		echo ourside
		echo "|||||||"
		echo original
		echo "======="
		echo theirside
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'failing checkout -b should not break working tree' '
	git reset --hard master &&
	git symbolic-ref HEAD refs/heads/master &&
	test_must_fail git checkout -b renamer side^ &&
	test $(git symbolic-ref HEAD) = refs/heads/master &&
	git diff --exit-code &&
	git diff --cached --exit-code

'

test_done
