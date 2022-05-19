#!/bin/sh

test_description='test auto-generated merge messages'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_oneline() {
	echo "$1" | sed "s/Q/'/g" >expect &&
	but log -1 --pretty=tformat:%s >actual &&
	test_cmp expect actual
}

test_expect_success 'merge local branch' '
	test_cummit main-1 &&
	but checkout -b local-branch &&
	test_cummit branch-1 &&
	but checkout main &&
	test_cummit main-2 &&
	but merge local-branch &&
	check_oneline "Merge branch Qlocal-branchQ"
'

test_expect_success 'merge octopus branches' '
	but checkout -b octopus-a main &&
	test_cummit octopus-1 &&
	but checkout -b octopus-b main &&
	test_cummit octopus-2 &&
	but checkout main &&
	but merge octopus-a octopus-b &&
	check_oneline "Merge branches Qoctopus-aQ and Qoctopus-bQ"
'

test_expect_success 'merge tag' '
	but checkout -b tag-branch main &&
	test_cummit tag-1 &&
	but checkout main &&
	test_cummit main-3 &&
	but merge tag-1 &&
	check_oneline "Merge tag Qtag-1Q"
'

test_expect_success 'ambiguous tag' '
	but checkout -b ambiguous main &&
	test_cummit ambiguous &&
	but checkout main &&
	test_cummit main-4 &&
	but merge ambiguous &&
	check_oneline "Merge tag QambiguousQ"
'

test_expect_success 'remote-tracking branch' '
	but checkout -b remote main &&
	test_cummit remote-1 &&
	but update-ref refs/remotes/origin/main remote &&
	but checkout main &&
	test_cummit main-5 &&
	but merge origin/main &&
	check_oneline "Merge remote-tracking branch Qorigin/mainQ"
'

test_done
