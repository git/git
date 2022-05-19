#!/bin/sh

test_description='ask merge-recursive to merge binary files'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	cat "$TEST_DIRECTORY"/test-binary-1.png >m &&
	but add m &&
	but ls-files -s | sed -e "s/ 0	/ 1	/" >E1 &&
	test_tick &&
	but cummit -m "initial" &&

	but branch side &&
	echo frotz >a &&
	but add a &&
	echo nitfol >>m &&
	but add a m &&
	but ls-files -s a >E0 &&
	but ls-files -s m | sed -e "s/ 0	/ 3	/" >E3 &&
	test_tick &&
	but cummit -m "main adds some" &&

	but checkout side &&
	echo rezrov >>m &&
	but add m &&
	but ls-files -s m | sed -e "s/ 0	/ 2	/" >E2 &&
	test_tick &&
	but cummit -m "side modifies" &&

	but tag anchor &&

	cat E0 E1 E2 E3 >expect
'

test_expect_success resolve '

	rm -f a* m* &&
	but reset --hard anchor &&

	test_must_fail but merge -s resolve main &&
	but ls-files -s >current &&
	test_cmp expect current
'

test_expect_success recursive '

	rm -f a* m* &&
	but reset --hard anchor &&

	test_must_fail but merge -s recursive main &&
	but ls-files -s >current &&
	test_cmp expect current
'

test_done
