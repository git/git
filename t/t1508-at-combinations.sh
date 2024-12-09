#!/bin/sh

test_description='test various @{X} syntax combinations together'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check() {
	test_expect_${4:-success} "$1 = $3" "
		echo '$3' >expect &&
		if test '$2' = 'commit'
		then
			git log -1 --format=%s '$1' >actual
		elif test '$2' = 'ref'
		then
			git rev-parse --symbolic-full-name '$1' >actual
		else
			git cat-file -p '$1' >actual
		fi &&
		test_cmp expect actual
	"
}

nonsense() {
	test_expect_${2:-success} "$1 is nonsensical" "
		test_must_fail git rev-parse --verify '$1'
	"
}

fail() {
	"$@" failure
}

test_expect_success 'setup' '
	test_commit main-one &&
	test_commit main-two &&
	git checkout -b upstream-branch &&
	test_commit upstream-one &&
	test_commit upstream-two &&
	if test_have_prereq !MINGW
	then
		git checkout -b @/at-test
	fi &&
	git checkout -b @@/at-test &&
	git checkout -b @at-test &&
	git checkout -b old-branch &&
	test_commit old-one &&
	test_commit old-two &&
	git checkout -b new-branch &&
	test_commit new-one &&
	test_commit new-two &&
	git branch -u main old-branch &&
	git branch -u upstream-branch new-branch
'

check HEAD ref refs/heads/new-branch
check "@{1}" commit new-one
check "HEAD@{1}" commit new-one
check "@{now}" commit new-two
check "HEAD@{now}" commit new-two
check "@{-1}" ref refs/heads/old-branch
check "@{-1}@{0}" commit old-two
check "@{-1}@{1}" commit old-one
check "@{u}" ref refs/heads/upstream-branch
check "HEAD@{u}" ref refs/heads/upstream-branch
check "@{u}@{1}" commit upstream-one
check "@{-1}@{u}" ref refs/heads/main
check "@{-1}@{u}@{1}" commit main-one
check "@" commit new-two
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

check "HEAD@{3}" commit old-two
nonsense "@{3}"

test_expect_success 'switch to old-branch' '
	git checkout old-branch
'

check HEAD ref refs/heads/old-branch
check "HEAD@{1}" commit new-two
check "@{1}" commit old-one

test_expect_success 'create path with @' '
	echo content >normal &&
	echo content >fun@ny &&
	git add normal fun@ny &&
	git commit -m "funny path"
'

check "@:normal" blob content
check "@:fun@ny" blob content

test_expect_success '@{1} works with only one reflog entry' '
	git checkout -B newbranch main &&
	git reflog expire --expire=now refs/heads/newbranch &&
	git commit --allow-empty -m "first after expiration" &&
	test_cmp_rev newbranch~ newbranch@{1}
'

test_expect_success '@{0} works with empty reflog' '
	git checkout -B newbranch main &&
	git reflog expire --expire=now refs/heads/newbranch &&
	test_cmp_rev newbranch newbranch@{0}
'

test_done
