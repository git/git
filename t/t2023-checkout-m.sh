#!/bin/sh

test_description='checkout -m -- <conflicted path>

Ensures that checkout -m on a resolved file restores the conflicted file'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	test_commit both.txt both.txt initial &&
	git branch topic &&
	test_commit modified_in_main both.txt in_main &&
	test_commit added_in_main each.txt in_main &&
	git checkout topic &&
	test_commit modified_in_topic both.txt in_topic &&
	test_commit added_in_topic each.txt in_topic
'

test_expect_success 'git merge main' '
    test_must_fail git merge main
'

clean_branchnames () {
	# Remove branch names after conflict lines
	sed 's/^\([<>]\{5,\}\) .*$/\1/'
}

test_expect_success '-m restores 2-way conflicted+resolved file' '
	cp each.txt each.txt.conflicted &&
	echo resolved >each.txt &&
	git add each.txt &&
	git checkout -m -- each.txt &&
	clean_branchnames <each.txt >each.txt.cleaned &&
	clean_branchnames <each.txt.conflicted >each.txt.conflicted.cleaned &&
	test_cmp each.txt.conflicted.cleaned each.txt.cleaned
'

test_expect_success '-m restores 3-way conflicted+resolved file' '
	cp both.txt both.txt.conflicted &&
	echo resolved >both.txt &&
	git add both.txt &&
	git checkout -m -- both.txt &&
	clean_branchnames <both.txt >both.txt.cleaned &&
	clean_branchnames <both.txt.conflicted >both.txt.conflicted.cleaned &&
	test_cmp both.txt.conflicted.cleaned both.txt.cleaned
'

test_expect_success 'force checkout a conflict file creates stage zero entry' '
	git init co-force &&
	(
		cd co-force &&
		echo a >a &&
		git add a &&
		git commit -ama &&
		A_OBJ=$(git rev-parse :a) &&
		git branch topic &&
		echo b >a &&
		git commit -amb &&
		B_OBJ=$(git rev-parse :a) &&
		git checkout topic &&
		echo c >a &&
		C_OBJ=$(git hash-object a) &&
		git checkout -m main &&
		test_cmp_rev :1:a $A_OBJ &&
		test_cmp_rev :2:a $B_OBJ &&
		test_cmp_rev :3:a $C_OBJ &&
		git checkout -f topic &&
		test_cmp_rev :0:a $A_OBJ
	)
'

test_done
