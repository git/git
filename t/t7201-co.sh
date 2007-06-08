#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git-checkout tests.

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

fill () {
	for i
	do
		echo "$i"
	done
}


test_expect_success setup '

	fill 1 2 3 4 5 6 7 8 >one &&
	fill a b c d e >two &&
	git add one two &&
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

test_expect_success "checkout -m with dirty tree" '

	git checkout -f master &&
	git clean &&

	fill 0 1 2 3 4 5 6 7 8 >one &&
	git checkout -m side &&

	test "$(git symbolic-ref HEAD)" = "refs/heads/side" &&

	fill "M	one" "A	three" "D	two" >expect.master &&
	git diff --name-status master >current.master &&
	diff expect.master current.master &&

	fill "M	one" >expect.side &&
	git diff --name-status side >current.side &&
	diff expect.side current.side &&

	: >expect.index &&
	git diff --cached >current.index &&
	diff expect.index current.index
'

test_expect_success "checkout -m with dirty tree, renamed" '

	git checkout -f master && git clean &&

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
	diff expect uno &&
	! test -f one &&
	git diff --cached >current &&
	! test -s current

'

test_expect_success 'checkout -m with merge conflict' '

	git checkout -f master && git clean &&

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
	diff current expect &&
	git diff --cached two >current &&
	! test -s current
'

test_expect_success 'checkout to detach HEAD' '

	git checkout -f renamer && git clean &&
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

test_expect_success 'checkout to detach HEAD with branchname^' '

	git checkout -f master && git clean &&
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

test_expect_success 'checkout to detach HEAD with HEAD^0' '

	git checkout -f master && git clean &&
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

test_done
