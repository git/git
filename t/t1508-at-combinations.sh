#!/bin/sh

test_description='test various @{X} syntax combinations together'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check() {
	test_expect_${4:-success} "$1 = $3" "
		echo '$3' >expect &&
		if test '$2' = 'cummit'
		then
			but log -1 --format=%s '$1' >actual
		elif test '$2' = 'ref'
		then
			but rev-parse --symbolic-full-name '$1' >actual
		else
			but cat-file -p '$1' >actual
		fi &&
		test_cmp expect actual
	"
}

nonsense() {
	test_expect_${2:-success} "$1 is nonsensical" "
		test_must_fail but rev-parse --verify '$1'
	"
}

fail() {
	"$@" failure
}

test_expect_success 'setup' '
	test_cummit main-one &&
	test_cummit main-two &&
	but checkout -b upstream-branch &&
	test_cummit upstream-one &&
	test_cummit upstream-two &&
	if test_have_prereq !MINGW
	then
		but checkout -b @/at-test
	fi &&
	but checkout -b @@/at-test &&
	but checkout -b @at-test &&
	but checkout -b old-branch &&
	test_cummit old-one &&
	test_cummit old-two &&
	but checkout -b new-branch &&
	test_cummit new-one &&
	test_cummit new-two &&
	but branch -u main old-branch &&
	but branch -u upstream-branch new-branch
'

check HEAD ref refs/heads/new-branch
check "@{1}" cummit new-one
check "HEAD@{1}" cummit new-one
check "@{now}" cummit new-two
check "HEAD@{now}" cummit new-two
check "@{-1}" ref refs/heads/old-branch
check "@{-1}@{0}" cummit old-two
check "@{-1}@{1}" cummit old-one
check "@{u}" ref refs/heads/upstream-branch
check "HEAD@{u}" ref refs/heads/upstream-branch
check "@{u}@{1}" cummit upstream-one
check "@{-1}@{u}" ref refs/heads/main
check "@{-1}@{u}@{1}" cummit main-one
check "@" cummit new-two
check "@@{u}" ref refs/heads/upstream-branch
check "@@/at-test" ref refs/heads/@@/at-test
test_have_prereq MINGW ||
check "@/at-test" ref refs/heads/@/at-test
check "@at-test" ref refs/heads/@at-test
nonsense "@{u}@{-1}"
nonsense "@{0}@{0}"
nonsense "@{1}@{u}"
nonsense "HEAD@{-1}"
nonsense "@{-1}@{-1}"

# @{N} versus HEAD@{N}

check "HEAD@{3}" cummit old-two
nonsense "@{3}"

test_expect_success 'switch to old-branch' '
	but checkout old-branch
'

check HEAD ref refs/heads/old-branch
check "HEAD@{1}" cummit new-two
check "@{1}" cummit old-one

test_expect_success 'create path with @' '
	echo content >normal &&
	echo content >fun@ny &&
	but add normal fun@ny &&
	but cummit -m "funny path"
'

check "@:normal" blob content
check "@:fun@ny" blob content

test_expect_success '@{1} works with only one reflog entry' '
	but checkout -B newbranch main &&
	but reflog expire --expire=now refs/heads/newbranch &&
	but cummit --allow-empty -m "first after expiration" &&
	test_cmp_rev newbranch~ newbranch@{1}
'

test_expect_success '@{0} works with empty reflog' '
	but checkout -B newbranch main &&
	but reflog expire --expire=now refs/heads/newbranch &&
	test_cmp_rev newbranch newbranch@{0}
'

test_done
