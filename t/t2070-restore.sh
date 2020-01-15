#!/bin/sh

test_description='restore basic functionality'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit first &&
	echo first-and-a-half >>first.t &&
	git add first.t &&
	test_commit second &&
	echo one >one &&
	echo two >two &&
	echo untracked >untracked &&
	echo ignored >ignored &&
	echo /ignored >.gitignore &&
	git add one two .gitignore &&
	git update-ref refs/heads/one master
'

test_expect_success 'restore without pathspec is not ok' '
	test_must_fail git restore &&
	test_must_fail git restore --source=first
'

test_expect_success 'restore a file, ignoring branch of same name' '
	cat one >expected &&
	echo dirty >>one &&
	git restore one &&
	test_cmp expected one
'

test_expect_success 'restore a file on worktree from another ref' '
	test_when_finished git reset --hard &&
	git cat-file blob first:./first.t >expected &&
	git restore --source=first first.t &&
	test_cmp expected first.t &&
	git cat-file blob HEAD:./first.t >expected &&
	git show :first.t >actual &&
	test_cmp expected actual
'

test_expect_success 'restore a file in the index from another ref' '
	test_when_finished git reset --hard &&
	git cat-file blob first:./first.t >expected &&
	git restore --source=first --staged first.t &&
	git show :first.t >actual &&
	test_cmp expected actual &&
	git cat-file blob HEAD:./first.t >expected &&
	test_cmp expected first.t
'

test_expect_success 'restore a file in both the index and worktree from another ref' '
	test_when_finished git reset --hard &&
	git cat-file blob first:./first.t >expected &&
	git restore --source=first --staged --worktree first.t &&
	git show :first.t >actual &&
	test_cmp expected actual &&
	test_cmp expected first.t
'

test_expect_success 'restore --staged uses HEAD as source' '
	test_when_finished git reset --hard &&
	git cat-file blob :./first.t >expected &&
	echo index-dirty >>first.t &&
	git add first.t &&
	git restore --staged first.t &&
	git cat-file blob :./first.t >actual &&
	test_cmp expected actual
'

test_expect_success 'restore --ignore-unmerged ignores unmerged entries' '
	git init unmerged &&
	(
		cd unmerged &&
		echo one >unmerged &&
		echo one >common &&
		git add unmerged common &&
		git commit -m common &&
		git switch -c first &&
		echo first >unmerged &&
		git commit -am first &&
		git switch -c second master &&
		echo second >unmerged &&
		git commit -am second &&
		test_must_fail git merge first &&

		echo dirty >>common &&
		test_must_fail git restore . &&

		git restore --ignore-unmerged --quiet . >output 2>&1 &&
		git diff common >diff-output &&
		test_must_be_empty output &&
		test_must_be_empty diff-output
	)
'

test_expect_success 'restore --staged adds deleted intent-to-add file back to index' '
	echo "nonempty" >nonempty &&
	>empty &&
	git add nonempty empty &&
	git commit -m "create files to be deleted" &&
	git rm --cached nonempty empty &&
	git add -N nonempty empty &&
	git restore --staged nonempty empty &&
	git diff --cached --exit-code
'

test_expect_success 'restore --staged invalidates cache tree for deletions' '
	test_when_finished git reset --hard &&
	>new1 &&
	>new2 &&
	git add new1 new2 &&

	# It is important to commit and then reset here, so that the index
	# contains a valid cache-tree for the "both" tree.
	git commit -m both &&
	git reset --soft HEAD^ &&

	git restore --staged new1 &&
	git commit -m "just new2" &&
	git rev-parse HEAD:new2 &&
	test_must_fail git rev-parse HEAD:new1
'

test_done
