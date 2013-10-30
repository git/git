#!/bin/sh

test_description='magic pathspec tests using git-log'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	test_tick &&
	git commit --allow-empty -m empty &&
	mkdir sub
'

test_expect_success '"git log :/" should not be ambiguous' '
	git log :/
'

test_expect_success '"git log :/a" should be ambiguous (applied both rev and worktree)' '
	: >a &&
	test_must_fail git log :/a 2>error &&
	grep ambiguous error
'

test_expect_success '"git log :/a -- " should not be ambiguous' '
	git log :/a --
'

test_expect_success '"git log -- :/a" should not be ambiguous' '
	git log -- :/a
'

test_expect_success '"git log :" should be ambiguous' '
	test_must_fail git log : 2>error &&
	grep ambiguous error
'

test_expect_success 'git log -- :' '
	git log -- :
'

test_expect_success 'git log HEAD -- :/' '
	cat >expected <<-EOF &&
	24b24cf initial
	EOF
	(cd sub && git log --oneline HEAD -- :/ >../actual) &&
	test_cmp expected actual
'

test_expect_success 'command line pathspec parsing for "git log"' '
	git reset --hard &&
	>a &&
	git add a &&
	git commit -m "add an empty a" --allow-empty &&
	echo 1 >a &&
	git commit -a -m "update a to 1" &&
	git checkout HEAD^ &&
	echo 2 >a &&
	git commit -a -m "update a to 2" &&
	test_must_fail git merge master &&
	git add a &&
	git log --merge -- a
'

test_done
