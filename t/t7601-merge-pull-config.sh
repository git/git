#!/bin/sh

test_description='git merge

Testing pull.* configuration parsing.'

. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 >c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 >c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 >c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2 &&
	git reset --hard c0 &&
	echo c3 >c3.c &&
	git add c3.c &&
	git commit -m c3 &&
	git tag c3
'

test_expect_success 'merge c1 with c2' '
	git reset --hard c1 &&
	test -f c0.c &&
	test -f c1.c &&
	test ! -f c2.c &&
	test ! -f c3.c &&
	git merge c2 &&
	test -f c1.c &&
	test -f c2.c
'

test_expect_success 'fast-forward pull succeeds with "true" in pull.ff' '
	git reset --hard c0 &&
	test_config pull.ff true &&
	git pull . c1 &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse c1)"
'

test_expect_success 'fast-forward pull creates merge with "false" in pull.ff' '
	git reset --hard c0 &&
	test_config pull.ff false &&
	git pull . c1 &&
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse c0)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse c1)"
'

test_expect_success 'pull prevents non-fast-forward with "only" in pull.ff' '
	git reset --hard c1 &&
	test_config pull.ff only &&
	test_must_fail git pull . c3
'

test_expect_success 'merge c1 with c2 (ours in pull.twohead)' '
	git reset --hard c1 &&
	git config pull.twohead ours &&
	git merge c2 &&
	test -f c1.c &&
	! test -f c2.c
'

test_expect_success 'merge c1 with c2 and c3 (recursive in pull.octopus)' '
	git reset --hard c1 &&
	git config pull.octopus "recursive" &&
	test_must_fail git merge c2 c3 &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD)"
'

test_expect_success 'merge c1 with c2 and c3 (recursive and octopus in pull.octopus)' '
	git reset --hard c1 &&
	git config pull.octopus "recursive octopus" &&
	git merge c2 c3 &&
	test "$(git rev-parse c1)" != "$(git rev-parse HEAD)" &&
	test "$(git rev-parse c1)" = "$(git rev-parse HEAD^1)" &&
	test "$(git rev-parse c2)" = "$(git rev-parse HEAD^2)" &&
	test "$(git rev-parse c3)" = "$(git rev-parse HEAD^3)" &&
	git diff --exit-code &&
	test -f c0.c &&
	test -f c1.c &&
	test -f c2.c &&
	test -f c3.c
'

conflict_count()
{
	{
		git diff-files --name-only
		git ls-files --unmerged
	} | wc -l
}

# c4 - c5
#    \ c6
#
# There are two conflicts here:
#
# 1) Because foo.c is renamed to bar.c, recursive will handle this,
# resolve won't.
#
# 2) One in conflict.c and that will always fail.

test_expect_success 'setup conflicted merge' '
	git reset --hard c0 &&
	echo A >conflict.c &&
	git add conflict.c &&
	echo contents >foo.c &&
	git add foo.c &&
	git commit -m c4 &&
	git tag c4 &&
	echo B >conflict.c &&
	git add conflict.c &&
	git mv foo.c bar.c &&
	git commit -m c5 &&
	git tag c5 &&
	git reset --hard c4 &&
	echo C >conflict.c &&
	git add conflict.c &&
	echo secondline >> foo.c &&
	git add foo.c &&
	git commit -m c6 &&
	git tag c6
'

# First do the merge with resolve and recursive then verify that
# recursive is chosen.

test_expect_success 'merge picks up the best result' '
	git config --unset-all pull.twohead &&
	git reset --hard c5 &&
	test_must_fail git merge -s resolve c6 &&
	resolve_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge -s recursive c6 &&
	recursive_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge -s recursive -s resolve c6 &&
	auto_count=$(conflict_count) &&
	test $auto_count = $recursive_count &&
	test $auto_count != $resolve_count
'

test_expect_success 'merge picks up the best result (from config)' '
	git config pull.twohead "recursive resolve" &&
	git reset --hard c5 &&
	test_must_fail git merge -s resolve c6 &&
	resolve_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge -s recursive c6 &&
	recursive_count=$(conflict_count) &&
	git reset --hard c5 &&
	test_must_fail git merge c6 &&
	auto_count=$(conflict_count) &&
	test $auto_count = $recursive_count &&
	test $auto_count != $resolve_count
'

test_expect_success 'merge errors out on invalid strategy' '
	git config pull.twohead "foobar" &&
	git reset --hard c5 &&
	test_must_fail git merge c6
'

test_expect_success 'merge errors out on invalid strategy' '
	git config --unset-all pull.twohead &&
	git reset --hard c5 &&
	test_must_fail git merge -s "resolve recursive" c6
'

test_done
