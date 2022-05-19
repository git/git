#!/bin/sh

test_description='restore basic functionality'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit first &&
	echo first-and-a-half >>first.t &&
	but add first.t &&
	test_cummit second &&
	echo one >one &&
	echo two >two &&
	echo untracked >untracked &&
	echo ignored >ignored &&
	echo /ignored >.butignore &&
	but add one two .butignore &&
	but update-ref refs/heads/one main
'

test_expect_success 'restore without pathspec is not ok' '
	test_must_fail but restore &&
	test_must_fail but restore --source=first
'

test_expect_success 'restore a file, ignoring branch of same name' '
	cat one >expected &&
	echo dirty >>one &&
	but restore one &&
	test_cmp expected one
'

test_expect_success 'restore a file on worktree from another ref' '
	test_when_finished but reset --hard &&
	but cat-file blob first:./first.t >expected &&
	but restore --source=first first.t &&
	test_cmp expected first.t &&
	but cat-file blob HEAD:./first.t >expected &&
	but show :first.t >actual &&
	test_cmp expected actual
'

test_expect_success 'restore a file in the index from another ref' '
	test_when_finished but reset --hard &&
	but cat-file blob first:./first.t >expected &&
	but restore --source=first --staged first.t &&
	but show :first.t >actual &&
	test_cmp expected actual &&
	but cat-file blob HEAD:./first.t >expected &&
	test_cmp expected first.t
'

test_expect_success 'restore a file in both the index and worktree from another ref' '
	test_when_finished but reset --hard &&
	but cat-file blob first:./first.t >expected &&
	but restore --source=first --staged --worktree first.t &&
	but show :first.t >actual &&
	test_cmp expected actual &&
	test_cmp expected first.t
'

test_expect_success 'restore --staged uses HEAD as source' '
	test_when_finished but reset --hard &&
	but cat-file blob :./first.t >expected &&
	echo index-dirty >>first.t &&
	but add first.t &&
	but restore --staged first.t &&
	but cat-file blob :./first.t >actual &&
	test_cmp expected actual
'

test_expect_success 'restore --worktree --staged uses HEAD as source' '
	test_when_finished but reset --hard &&
	but show HEAD:./first.t >expected &&
	echo dirty >>first.t &&
	but add first.t &&
	but restore --worktree --staged first.t &&
	but show :./first.t >actual &&
	test_cmp expected actual &&
	test_cmp expected first.t
'

test_expect_success 'restore --ignore-unmerged ignores unmerged entries' '
	but init unmerged &&
	(
		cd unmerged &&
		echo one >unmerged &&
		echo one >common &&
		but add unmerged common &&
		but cummit -m common &&
		but switch -c first &&
		echo first >unmerged &&
		but cummit -am first &&
		but switch -c second main &&
		echo second >unmerged &&
		but cummit -am second &&
		test_must_fail but merge first &&

		echo dirty >>common &&
		test_must_fail but restore . &&

		but restore --ignore-unmerged --quiet . >output 2>&1 &&
		but diff common >diff-output &&
		test_must_be_empty output &&
		test_must_be_empty diff-output
	)
'

test_expect_success 'restore --staged adds deleted intent-to-add file back to index' '
	echo "nonempty" >nonempty &&
	>empty &&
	but add nonempty empty &&
	but cummit -m "create files to be deleted" &&
	but rm --cached nonempty empty &&
	but add -N nonempty empty &&
	but restore --staged nonempty empty &&
	but diff --cached --exit-code
'

test_expect_success 'restore --staged invalidates cache tree for deletions' '
	test_when_finished but reset --hard &&
	>new1 &&
	>new2 &&
	but add new1 new2 &&

	# It is important to cummit and then reset here, so that the index
	# contains a valid cache-tree for the "both" tree.
	but cummit -m both &&
	but reset --soft HEAD^ &&

	but restore --staged new1 &&
	but cummit -m "just new2" &&
	but rev-parse HEAD:new2 &&
	test_must_fail but rev-parse HEAD:new1
'

test_done
