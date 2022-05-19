#!/bin/sh

test_description='test cherry-picking with --ff option'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo first > file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m "first" &&
	but tag first &&

	but checkout -b other &&
	echo second >> file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m "second" &&
	but tag second &&
	test_oid_cache <<-EOF
	cp_ff sha1:1df192cd8bc58a2b275d842cede4d221ad9000d1
	cp_ff sha256:e70d6b7fc064bddb516b8d512c9057094b96ce6ff08e12080acc4fe7f1d60a1d
	EOF
'

test_expect_success 'cherry-pick using --ff fast forwards' '
	but checkout main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick --ff second &&
	test "$(but rev-parse --verify HEAD)" = "$(but rev-parse --verify second)"
'

test_expect_success 'cherry-pick not using --ff does not fast forwards' '
	but checkout main &&
	but reset --hard first &&
	test_tick &&
	but cherry-pick second &&
	test "$(but rev-parse --verify HEAD)" != "$(but rev-parse --verify second)"
'

#
# We setup the following graph:
#
#	      B---C
#	     /   /
#	first---A
#
# (This has been taken from t3502-cherry-pick-merge.sh)
#
test_expect_success 'merge setup' '
	but checkout main &&
	but reset --hard first &&
	echo new line >A &&
	but add A &&
	test_tick &&
	but cummit -m "add line to A" A &&
	but tag A &&
	but checkout -b side first &&
	echo new line >B &&
	but add B &&
	test_tick &&
	but cummit -m "add line to B" B &&
	but tag B &&
	but checkout main &&
	but merge side &&
	but tag C &&
	but checkout -b new A
'

test_expect_success 'cherry-pick explicit first parent of a non-merge with --ff' '
	but reset --hard A -- &&
	but cherry-pick --ff -m 1 B &&
	but diff --exit-code C --
'

test_expect_success 'cherry pick a merge with --ff but without -m should fail' '
	but reset --hard A -- &&
	test_must_fail but cherry-pick --ff C &&
	but diff --exit-code A --
'

test_expect_success 'cherry pick with --ff a merge (1)' '
	but reset --hard A -- &&
	but cherry-pick --ff -m 1 C &&
	but diff --exit-code C &&
	test "$(but rev-parse --verify HEAD)" = "$(but rev-parse --verify C)"
'

test_expect_success 'cherry pick with --ff a merge (2)' '
	but reset --hard B -- &&
	but cherry-pick --ff -m 2 C &&
	but diff --exit-code C &&
	test "$(but rev-parse --verify HEAD)" = "$(but rev-parse --verify C)"
'

test_expect_success 'cherry pick a merge relative to nonexistent parent with --ff should fail' '
	but reset --hard B -- &&
	test_must_fail but cherry-pick --ff -m 3 C
'

test_expect_success 'cherry pick a root cummit with --ff' '
	but reset --hard first -- &&
	but rm file1 &&
	echo first >file2 &&
	but add file2 &&
	but cummit --amend -m "file2" &&
	but cherry-pick --ff first &&
	test "$(but rev-parse --verify HEAD)" = "$(test_oid cp_ff)"
'

test_expect_success 'cherry-pick --ff on unborn branch' '
	but checkout --orphan unborn &&
	but rm --cached -r . &&
	rm -rf * &&
	but cherry-pick --ff first &&
	test_cmp_rev first HEAD
'

test_done
