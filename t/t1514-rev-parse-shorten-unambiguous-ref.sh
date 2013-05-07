#!/bin/sh

test_description='short refname disambiguation

Create refs that share the same name, and make sure
"git rev-parse --abbrev-ref" can present them all with as
short a name as possible, while still being unambiguous.
'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit master_a &&
	git remote add origin . &&
	git fetch origin &&
	test_commit master_b &&
	git branch origin/master &&
	test_commit master_c &&
	git tag master &&
	test_commit master_d &&
	git update-ref refs/master master_d &&
	test_commit master_e &&
	git update-ref refs/remotes/origin/HEAD master_e &&
	test_commit master_f &&
	cat >expect.show-ref <<-EOF
	$(git rev-parse master_f) refs/heads/master
	$(git rev-parse master_b) refs/heads/origin/master
	$(git rev-parse master_d) refs/master
	$(git rev-parse master_e) refs/remotes/origin/HEAD
	$(git rev-parse master_a) refs/remotes/origin/master
	$(git rev-parse master_c) refs/tags/master
	$(git rev-parse master_a) refs/tags/master_a
	$(git rev-parse master_b) refs/tags/master_b
	$(git rev-parse master_c) refs/tags/master_c
	$(git rev-parse master_d) refs/tags/master_d
	$(git rev-parse master_e) refs/tags/master_e
	$(git rev-parse master_f) refs/tags/master_f
	EOF
'

test_expect_success 'we have the expected ref layout' '
	git show-ref >actual.show-ref &&
	test_cmp expect.show-ref actual.show-ref
'

test_shortname () {
	refname=$1
	mode=$2
	expect_shortname=$3
	expect_sha1=$4
	echo "$expect_shortname" >expect.shortname &&
	actual_shortname="$(git rev-parse --abbrev-ref="$mode" "$refname")" &&
	echo "$actual_shortname" >actual.shortname &&
	test_cmp expect.shortname actual.shortname &&
	git rev-parse --verify "$expect_sha1" >expect.sha1 &&
	git rev-parse --verify "$actual_shortname" >actual.sha1 &&
	test_cmp expect.sha1 actual.sha1
}

test_expect_failure 'shortening refnames in strict mode' '
	test_shortname refs/heads/master strict heads/master master_f &&
	test_shortname refs/heads/origin/master strict heads/origin/master master_b &&
	test_shortname refs/master strict refs/master master_d &&
	test_shortname refs/remotes/origin/HEAD strict origin master_e &&
	test_shortname refs/remotes/origin/master strict remotes/origin/master master_a &&
	test_shortname refs/tags/master strict tags/master master_c
'

test_expect_failure 'shortening refnames in loose mode' '
	test_shortname refs/heads/master loose heads/master master_f &&
	test_shortname refs/heads/origin/master loose origin/master master_b &&
	test_shortname refs/master loose master master_d &&
	test_shortname refs/remotes/origin/HEAD loose origin master_e &&
	test_shortname refs/remotes/origin/master loose remotes/origin/master master_a &&
	test_shortname refs/tags/master loose tags/master master_c
'

test_done
