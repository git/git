#!/bin/sh

test_description='git rebase - test patch id computation'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
	git checkout -q -f main &&
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

	git cherry-pick main >/dev/null 2>&1 &&

	git branch -f squashed main &&
	git checkout -q -f squashed &&
	git reset -q --soft HEAD~2 &&
	git commit -q -m squashed &&

	git branch -f mode main &&
	git checkout -q -f mode &&
	test_chmod +x file &&
	git commit -q -a --amend &&

	git branch -f modeother other &&
	git checkout -q -f modeother &&
	test_chmod +x file &&
	git commit -q -a --amend
'

test_expect_success 'detect upstream patch' '
	git checkout -q main^{} &&
	scramble file &&
	git add file &&
	git commit -q -m "change big file again" &&
	git checkout -q other^{} &&
	git rebase main &&
	git rev-list main...HEAD~ >revs &&
	test_must_be_empty revs
'

test_expect_success 'detect upstream patch binary' '
	echo "file binary" >.gitattributes &&
	git checkout -q other^{} &&
	git rebase main &&
	git rev-list main...HEAD~ >revs &&
	test_must_be_empty revs &&
	test_when_finished "rm .gitattributes"
'

test_expect_success 'detect upstream patch modechange' '
	git checkout -q modeother^{} &&
	git rebase mode &&
	git rev-list mode...HEAD~ >revs &&
	test_must_be_empty revs
'

test_expect_success 'do not drop patch' '
	git checkout -q other^{} &&
	test_must_fail git rebase squashed &&
	test_when_finished "git rebase --abort"
'

test_expect_success 'do not drop patch binary' '
	echo "file binary" >.gitattributes &&
	git checkout -q other^{} &&
	test_must_fail git rebase squashed &&
	test_when_finished "git rebase --abort" &&
	test_when_finished "rm .gitattributes"
'

test_expect_success 'do not drop patch modechange' '
	git checkout -q modeother^{} &&
	git rebase other &&
	cat >expected <<-\EOF &&
	diff --git a/file b/file
	old mode 100644
	new mode 100755
	EOF
	git diff HEAD~ >modediff &&
	test_cmp expected modediff
'

test_done
