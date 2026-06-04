#!/bin/sh

test_description='tagopt variable affects "git fetch" and is overridden by commandline.'

. ./test-lib.sh

setup_clone () {
	git clone --mirror . $1 &&
	git remote add remote_$1 $1 &&
	(cd $1 &&
	git tag tag_$1 &&
	git branch branch_$1)
}

test_expect_success setup '
	test_commit test &&
	setup_clone one &&
	git config remote.remote_one.tagopt --no-tags &&
	setup_clone two &&
	git config remote.remote_two.tagopt --tags
	'

test_expect_success "fetch with tagopt=--no-tags does not get tag" '
	git fetch remote_one &&
	test_must_fail git show-ref tag_one &&
	git show-ref remote_one/branch_one
	'

test_expect_success "fetch --tags with tagopt=--no-tags gets tag" '
	(
		cd one &&
		git branch second_branch_one
	) &&
	git fetch --tags remote_one &&
	git show-ref tag_one &&
	git show-ref remote_one/second_branch_one
	'

test_expect_success "fetch --no-tags with tagopt=--tags does not get tag" '
	git fetch --no-tags remote_two &&
	test_must_fail git show-ref tag_two &&
	git show-ref remote_two/branch_two
	'

test_expect_success "fetch with tagopt=--tags gets tag" '
	(
		cd two &&
		git branch second_branch_two
	) &&
	git fetch remote_two &&
	git show-ref tag_two &&
	git show-ref remote_two/second_branch_two
	'
test_done
