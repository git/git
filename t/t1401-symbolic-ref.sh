#!/bin/sh

test_description='basic symbolic-ref tests'
. ./test-lib.sh

# If the tests munging HEAD fail, they can break detection of
# the git repo, meaning that further tests will operate on
# the surrounding git repo instead of the trash directory.
reset_to_sane() {
	echo ref: refs/heads/foo >.git/HEAD
}

test_expect_success 'symbolic-ref writes HEAD' '
	git symbolic-ref HEAD refs/heads/foo &&
	echo ref: refs/heads/foo >expect &&
	test_cmp expect .git/HEAD
'

test_expect_success 'symbolic-ref reads HEAD' '
	echo refs/heads/foo >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref refuses non-ref for HEAD' '
	test_must_fail git symbolic-ref HEAD foo
'
reset_to_sane

test_expect_success 'symbolic-ref refuses bare sha1' '
	echo content >file && git add file && git commit -m one &&
	test_must_fail git symbolic-ref HEAD $(git rev-parse HEAD)
'
reset_to_sane

test_expect_success 'HEAD cannot be removed' '
	test_must_fail git symbolic-ref -d HEAD
'

reset_to_sane

test_expect_success 'symbolic-ref can be deleted' '
	git symbolic-ref NOTHEAD refs/heads/foo &&
	git symbolic-ref -d NOTHEAD &&
	test_path_is_file .git/refs/heads/foo &&
	test_path_is_missing .git/NOTHEAD
'
reset_to_sane

test_expect_success 'symbolic-ref can delete dangling symref' '
	git symbolic-ref NOTHEAD refs/heads/missing &&
	git symbolic-ref -d NOTHEAD &&
	test_path_is_missing .git/refs/heads/missing &&
	test_path_is_missing .git/NOTHEAD
'
reset_to_sane

test_expect_success 'symbolic-ref fails to delete missing FOO' '
	echo "fatal: Cannot delete FOO, not a symbolic ref" >expect &&
	test_must_fail git symbolic-ref -d FOO >actual 2>&1 &&
	test_cmp expect actual
'
reset_to_sane

test_expect_success 'symbolic-ref fails to delete real ref' '
	echo "fatal: Cannot delete refs/heads/foo, not a symbolic ref" >expect &&
	test_must_fail git symbolic-ref -d refs/heads/foo >actual 2>&1 &&
	test_path_is_file .git/refs/heads/foo &&
	test_cmp expect actual
'
reset_to_sane

test_expect_success 'create large ref name' '
	# make 256+ character ref; some systems may not handle that,
	# so be gentle
	long=0123456789abcdef &&
	long=$long/$long/$long/$long &&
	long=$long/$long/$long/$long &&
	long_ref=refs/heads/$long &&
	tree=$(git write-tree) &&
	commit=$(echo foo | git commit-tree $tree) &&
	if git update-ref $long_ref $commit; then
		test_set_prereq LONG_REF
	else
		echo >&2 "long refs not supported"
	fi
'

test_expect_success LONG_REF 'symbolic-ref can point to large ref name' '
	git symbolic-ref HEAD $long_ref &&
	echo $long_ref >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual
'

test_expect_success LONG_REF 'we can parse long symbolic ref' '
	echo $commit >expect &&
	git rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref reports failure in exit code' '
	test_when_finished "rm -f .git/HEAD.lock" &&
	>.git/HEAD.lock &&
	test_must_fail git symbolic-ref HEAD refs/heads/whatever
'

test_expect_success 'symbolic-ref writes reflog entry' '
	git checkout -b log1 &&
	test_commit one &&
	git checkout -b log2  &&
	test_commit two &&
	git checkout --orphan orphan &&
	git symbolic-ref -m create HEAD refs/heads/log1 &&
	git symbolic-ref -m update HEAD refs/heads/log2 &&
	cat >expect <<-\EOF &&
	update
	create
	EOF
	git log --format=%gs -g -2 >actual &&
	test_cmp expect actual
'

test_expect_success 'symbolic-ref does not create ref d/f conflicts' '
	git checkout -b df &&
	test_commit df &&
	test_must_fail git symbolic-ref refs/heads/df/conflict refs/heads/df &&
	git pack-refs --all --prune &&
	test_must_fail git symbolic-ref refs/heads/df/conflict refs/heads/df
'

test_expect_success 'symbolic-ref handles existing pointer to invalid name' '
	head=$(git rev-parse HEAD) &&
	git symbolic-ref HEAD refs/heads/outer &&
	git update-ref refs/heads/outer/inner $head &&
	git symbolic-ref HEAD refs/heads/unrelated
'

test_done
