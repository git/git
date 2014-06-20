#!/bin/sh

test_description='git rebase - test patch id computation'

. ./test-lib.sh

test -n "$GIT_PATCHID_TIMING_TESTS" && test_set_prereq EXPENSIVE

count () {
	i=0
	while test $i -lt $1
	do
		echo "$i"
		i=$(($i+1))
	done
}

scramble () {
	i=0
	while read x
	do
		if test $i -ne 0
		then
			echo "$x"
		fi
		i=$((($i+1) % 10))
	done <"$1" >"$1.new"
	mv -f "$1.new" "$1"
}

run () {
	echo \$ "$@"
	/usr/bin/time "$@" >/dev/null
}

test_expect_success 'setup' '
	git commit --allow-empty -m initial &&
	git tag root
'

do_tests () {
	nlines=$1 pr=${2-}

	test_expect_success $pr "setup: $nlines lines" "
		rm -f .gitattributes &&
		git checkout -q -f master &&
		git reset --hard root &&
		count $nlines >file &&
		git add file &&
		git commit -q -m initial &&
		git branch -f other &&

		scramble file &&
		git add file &&
		git commit -q -m 'change big file' &&

		git checkout -q other &&
		: >newfile &&
		git add newfile &&
		git commit -q -m 'add small file' &&

		git cherry-pick master >/dev/null 2>&1
	"

	test_debug "
		run git diff master^\!
	"

	test_expect_success $pr 'setup attributes' "
		echo 'file binary' >.gitattributes
	"

	test_debug "
		run git format-patch --stdout master &&
		run git format-patch --stdout --ignore-if-in-upstream master
	"

	test_expect_success $pr 'detect upstream patch' "
		git checkout -q master &&
		scramble file &&
		git add file &&
		git commit -q -m 'change big file again' &&
		git checkout -q other^{} &&
		git rebase master &&
		test_must_fail test -n \"\$(git rev-list master...HEAD~)\"
	"

	test_expect_success $pr 'do not drop patch' "
		git branch -f squashed master &&
		git checkout -q -f squashed &&
		git reset -q --soft HEAD~2 &&
		git commit -q -m squashed &&
		git checkout -q other^{} &&
		test_must_fail git rebase squashed &&
		rm -rf .git/rebase-apply
	"
}

do_tests 500
do_tests 50000 EXPENSIVE

test_done
