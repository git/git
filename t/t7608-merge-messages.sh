#!/bin/sh

test_description='test auto-generated merge messages'
. ./test-lib.sh

check_oneline() {
	echo "$1" | sed "s/Q/'/g" >expect &&
	git log -1 --pretty=tformat:%s >actual &&
	test_cmp expect actual
}

test_expect_success 'merge local branch' '
	test_commit master-1 &&
	git checkout -b local-branch &&
	test_commit branch-1 &&
	git checkout master &&
	test_commit master-2 &&
	git merge local-branch &&
	check_oneline "Merge branch Qlocal-branchQ"
'

test_expect_success 'merge octopus branches' '
	git checkout -b octopus-a master &&
	test_commit octopus-1 &&
	git checkout -b octopus-b master &&
	test_commit octopus-2 &&
	git checkout master &&
	git merge octopus-a octopus-b &&
	check_oneline "Merge branches Qoctopus-aQ and Qoctopus-bQ"
'

test_expect_success 'merge tag' '
	git checkout -b tag-branch master &&
	test_commit tag-1 &&
	git checkout master &&
	test_commit master-3 &&
	git merge tag-1 &&
	check_oneline "Merge commit Qtag-1Q"
'

test_expect_success 'ambiguous tag' '
	git checkout -b ambiguous master &&
	test_commit ambiguous &&
	git checkout master &&
	test_commit master-4 &&
	git merge ambiguous &&
	check_oneline "Merge commit QambiguousQ"
'

test_expect_success 'remote branch' '
	git checkout -b remote master &&
	test_commit remote-1 &&
	git update-ref refs/remotes/origin/master remote &&
	git checkout master &&
	test_commit master-5 &&
	git merge origin/master &&
	check_oneline "Merge remote branch Qorigin/masterQ"
'

test_done
