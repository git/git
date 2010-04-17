#!/bin/sh

test_description='combined diff'

. ./test-lib.sh

setup_helper () {
	one=$1 branch=$2 side=$3 &&

	git branch $side $branch &&
	for l in $one two three fyra
	do
		echo $l
	done >file &&
	git add file &&
	test_tick &&
	git commit -m $branch &&
	git checkout $side &&
	for l in $one two three quatro
	do
		echo $l
	done >file &&
	git add file &&
	test_tick &&
	git commit -m $side &&
	test_must_fail git merge $branch &&
	for l in $one three four
	do
		echo $l
	done >file &&
	git add file &&
	test_tick &&
	git commit -m "merge $branch into $side"
}

verify_helper () {
	it=$1 &&

	# Ignore lines that were removed only from the other parent
	sed -e '
		1,/^@@@/d
		/^ -/d
		s/^\(.\)./\1/
	' "$it" >"$it.actual.1" &&
	sed -e '
		1,/^@@@/d
		/^- /d
		s/^.\(.\)/\1/
	' "$it" >"$it.actual.2" &&

	git diff "$it^" "$it" -- | sed -e '1,/^@@/d' >"$it.expect.1" &&
	test_cmp "$it.expect.1" "$it.actual.1" &&

	git diff "$it^2" "$it" -- | sed -e '1,/^@@/d' >"$it.expect.2" &&
	test_cmp "$it.expect.2" "$it.actual.2"
}

test_expect_success setup '
	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&

	git branch withone &&
	git branch sansone &&

	git checkout withone &&
	setup_helper one withone sidewithone &&

	git checkout sansone &&
	setup_helper "" sansone sidesansone
'

test_expect_success 'check combined output (1)' '
	git show sidewithone -- >sidewithone &&
	verify_helper sidewithone
'

test_expect_success 'check combined output (2)' '
	git show sidesansone -- >sidesansone &&
	verify_helper sidesansone
'

test_expect_success 'diagnose truncated file' '
	>file &&
	git add file &&
	git commit --amend -C HEAD &&
	git show >out &&
	grep "diff --cc file" out
'

test_done
