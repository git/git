#!/bin/sh

test_description='tagopt variable affects "but fetch" and is overridden by commandline.'

. ./test-lib.sh

setup_clone () {
	but clone --mirror . $1 &&
	but remote add remote_$1 $1 &&
	(cd $1 &&
	but tag tag_$1 &&
	but branch branch_$1)
}

test_expect_success setup '
	test_cummit test &&
	setup_clone one &&
	but config remote.remote_one.tagopt --no-tags &&
	setup_clone two &&
	but config remote.remote_two.tagopt --tags
	'

test_expect_success "fetch with tagopt=--no-tags does not get tag" '
	but fetch remote_one &&
	test_must_fail but show-ref tag_one &&
	but show-ref remote_one/branch_one
	'

test_expect_success "fetch --tags with tagopt=--no-tags gets tag" '
	(
		cd one &&
		but branch second_branch_one
	) &&
	but fetch --tags remote_one &&
	but show-ref tag_one &&
	but show-ref remote_one/second_branch_one
	'

test_expect_success "fetch --no-tags with tagopt=--tags does not get tag" '
	but fetch --no-tags remote_two &&
	test_must_fail but show-ref tag_two &&
	but show-ref remote_two/branch_two
	'

test_expect_success "fetch with tagopt=--tags gets tag" '
	(
		cd two &&
		but branch second_branch_two
	) &&
	but fetch remote_two &&
	but show-ref tag_two &&
	but show-ref remote_two/second_branch_two
	'
test_done
