#!/bin/sh

test_description='checkout -m -- <conflicted path>

Ensures that checkout -m on a resolved file restores the conflicted file'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	test_cummit both.txt both.txt initial &&
	but branch topic &&
	test_cummit modified_in_main both.txt in_main &&
	test_cummit added_in_main each.txt in_main &&
	but checkout topic &&
	test_cummit modified_in_topic both.txt in_topic &&
	test_cummit added_in_topic each.txt in_topic
'

test_expect_success 'but merge main' '
    test_must_fail but merge main
'

clean_branchnames () {
	# Remove branch names after conflict lines
	sed 's/^\([<>]\{5,\}\) .*$/\1/'
}

test_expect_success '-m restores 2-way conflicted+resolved file' '
	cp each.txt each.txt.conflicted &&
	echo resolved >each.txt &&
	but add each.txt &&
	but checkout -m -- each.txt &&
	clean_branchnames <each.txt >each.txt.cleaned &&
	clean_branchnames <each.txt.conflicted >each.txt.conflicted.cleaned &&
	test_cmp each.txt.conflicted.cleaned each.txt.cleaned
'

test_expect_success '-m restores 3-way conflicted+resolved file' '
	cp both.txt both.txt.conflicted &&
	echo resolved >both.txt &&
	but add both.txt &&
	but checkout -m -- both.txt &&
	clean_branchnames <both.txt >both.txt.cleaned &&
	clean_branchnames <both.txt.conflicted >both.txt.conflicted.cleaned &&
	test_cmp both.txt.conflicted.cleaned both.txt.cleaned
'

test_expect_success 'force checkout a conflict file creates stage zero entry' '
	but init co-force &&
	(
		cd co-force &&
		echo a >a &&
		but add a &&
		but cummit -ama &&
		A_OBJ=$(but rev-parse :a) &&
		but branch topic &&
		echo b >a &&
		but cummit -amb &&
		B_OBJ=$(but rev-parse :a) &&
		but checkout topic &&
		echo c >a &&
		C_OBJ=$(but hash-object a) &&
		but checkout -m main &&
		test_cmp_rev :1:a $A_OBJ &&
		test_cmp_rev :2:a $B_OBJ &&
		test_cmp_rev :3:a $C_OBJ &&
		but checkout -f topic &&
		test_cmp_rev :0:a $A_OBJ
	)
'

test_done
