#!/bin/sh

test_description='test various @{X} syntax combinations together'
. ./test-lib.sh

check() {
test_expect_${3:-success} "$1 = $2" "
	echo '$2' >expect &&
	git log -1 --format=%s '$1' >actual &&
	test_cmp expect actual
"
}
nonsense() {
test_expect_${2:-success} "$1 is nonsensical" "
	test_must_fail git log -1 '$1'
"
}
fail() {
	"$@" failure
}

test_expect_success 'setup' '
	test_commit master-one &&
	test_commit master-two &&
	git checkout -b upstream-branch &&
	test_commit upstream-one &&
	test_commit upstream-two &&
	git checkout -b old-branch &&
	test_commit old-one &&
	test_commit old-two &&
	git checkout -b new-branch &&
	test_commit new-one &&
	test_commit new-two &&
	git config branch.old-branch.remote . &&
	git config branch.old-branch.merge refs/heads/master &&
	git config branch.new-branch.remote . &&
	git config branch.new-branch.merge refs/heads/upstream-branch
'

check HEAD new-two
check "@{1}" new-one
check "@{-1}" old-two
check "@{-1}@{1}" old-one
check "@{u}" upstream-two
check "@{u}@{1}" upstream-one
check "@{-1}@{u}" master-two
check "@{-1}@{u}@{1}" master-one
nonsense "@{u}@{-1}"
nonsense "@{1}@{u}"

test_done
