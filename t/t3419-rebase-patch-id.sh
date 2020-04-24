#!/bin/sh

test_description='git rebase - test patch id computation'

. ./test-lib.sh

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

test_expect_success 'setup' '
	git commit --allow-empty -m initial &&
	git tag root
'

test_expect_success 'setup: 500 lines' '
	rm -f .gitattributes &&
	git checkout -q -f master &&
	git reset --hard root &&
	test_seq 500 >file &&
	git add file &&
	git commit -q -m initial &&
	git branch -f other &&

	scramble file &&
	git add file &&
	git commit -q -m "change big file" &&

	git checkout -q other &&
	: >newfile &&
	git add newfile &&
	git commit -q -m "add small file" &&

	git cherry-pick master >/dev/null 2>&1
'

test_expect_success 'setup attributes' '
	echo "file binary" >.gitattributes
'

test_expect_success 'detect upstream patch' '
	git checkout -q master &&
	scramble file &&
	git add file &&
	git commit -q -m "change big file again" &&
	git checkout -q other^{} &&
	git rebase master &&
	git rev-list master...HEAD~ >revs &&
	test_must_be_empty revs
'

test_expect_success 'do not drop patch' '
	git branch -f squashed master &&
	git checkout -q -f squashed &&
	git reset -q --soft HEAD~2 &&
	git commit -q -m squashed &&
	git checkout -q other^{} &&
	test_must_fail git rebase squashed &&
	git rebase --quit
'

test_done
