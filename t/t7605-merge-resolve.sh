#!/bin/sh

test_description='git merge

Testing the resolve strategy.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 > c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 > c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 > c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2 &&
	git reset --hard c0 &&
	echo c3 > c2.c &&
	git add c2.c &&
	git commit -m c3 &&
	git tag c3
'

merge_c1_to_c2_cmds='
	git reset --hard c1 &&
	git merge -s resolve c2 &&
	test "$(git rev-parse c1)" != "$(git rev-parse HEAD)" &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD^1)" &&
	test "$(git rev-parse c2)" = "$(git rev-parse HEAD^2)" &&
	git diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test 3 = $(git ls-tree -r HEAD | wc -l) &&
	test 3 = $(git ls-files | wc -l)
'

test_expect_success 'merge c1 to c2'        "$merge_c1_to_c2_cmds"

test_expect_success 'merge c1 to c2, again' "$merge_c1_to_c2_cmds"

test_expect_success 'merge c2 to c3 (fails)' '
	git reset --hard c2 &&
	test_must_fail git merge -s resolve c3
'
test_done
