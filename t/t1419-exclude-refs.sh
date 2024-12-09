#!/bin/sh

test_description='test exclude_patterns functionality in main ref store'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

for_each_ref__exclude () {
	GIT_TRACE2_PERF=1 test-tool ref-store main \
		for-each-ref--exclude "$@" >actual.raw
	cut -d ' ' -f 2 actual.raw
}

for_each_ref () {
	git for-each-ref --format='%(refname)' "$@"
}

assert_jumps () {
	local nr="$1"
	local trace="$2"

	case "$GIT_DEFAULT_REF_FORMAT" in
	files)
		grep -q "name:jumps_made value:$nr$" $trace;;
	reftable)
		grep -q "name:reseeks_made value:$nr$" $trace;;
	*)
		BUG "unhandled ref format $GIT_DEFAULT_REF_FORMAT";;
	esac
}

assert_no_jumps () {
	! assert_jumps ".*" "$1"
}

test_expect_success 'setup' '
	test_commit --no-tag base &&
	base="$(git rev-parse HEAD)" &&

	for name in foo bar baz quux
	do
		for i in 1 2 3
		do
			echo "create refs/heads/$name/$i $base" || return 1
		done || return 1
	done >in &&
	echo "delete refs/heads/main" >>in &&

	git update-ref --stdin <in &&
	git pack-refs --all
'

test_expect_success 'excluded region in middle' '
	for_each_ref__exclude refs/heads refs/heads/foo >actual 2>perf &&
	for_each_ref refs/heads/bar refs/heads/baz refs/heads/quux >expect &&

	test_cmp expect actual &&
	assert_jumps 1 perf
'

test_expect_success 'excluded region at beginning' '
	for_each_ref__exclude refs/heads refs/heads/bar >actual 2>perf &&
	for_each_ref refs/heads/baz refs/heads/foo refs/heads/quux >expect &&

	test_cmp expect actual &&
	assert_jumps 1 perf
'

test_expect_success 'excluded region at end' '
	for_each_ref__exclude refs/heads refs/heads/quux >actual 2>perf &&
	for_each_ref refs/heads/foo refs/heads/bar refs/heads/baz >expect &&

	test_cmp expect actual &&
	assert_jumps 1 perf
'

test_expect_success 'disjoint excluded regions' '
	for_each_ref__exclude refs/heads refs/heads/bar refs/heads/quux >actual 2>perf &&
	for_each_ref refs/heads/baz refs/heads/foo >expect &&

	test_cmp expect actual &&
	assert_jumps 2 perf
'

test_expect_success 'adjacent, non-overlapping excluded regions' '
	for_each_ref__exclude refs/heads refs/heads/bar refs/heads/baz >actual 2>perf &&
	for_each_ref refs/heads/foo refs/heads/quux >expect &&

	test_cmp expect actual &&
	case "$GIT_DEFAULT_REF_FORMAT" in
	files)
		assert_jumps 1 perf;;
	reftable)
		assert_jumps 2 perf;;
	*)
		BUG "unhandled ref format $GIT_DEFAULT_REF_FORMAT";;
	esac
'

test_expect_success 'overlapping excluded regions' '
	for_each_ref__exclude refs/heads refs/heads/ba refs/heads/baz >actual 2>perf &&
	for_each_ref refs/heads/foo refs/heads/quux >expect &&

	test_cmp expect actual &&
	assert_jumps 1 perf
'

test_expect_success 'several overlapping excluded regions' '
	for_each_ref__exclude refs/heads \
		refs/heads/bar refs/heads/baz refs/heads/foo >actual 2>perf &&
	for_each_ref refs/heads/quux >expect &&

	test_cmp expect actual &&
	case "$GIT_DEFAULT_REF_FORMAT" in
	files)
		assert_jumps 1 perf;;
	reftable)
		assert_jumps 3 perf;;
	*)
		BUG "unhandled ref format $GIT_DEFAULT_REF_FORMAT";;
	esac
'

test_expect_success 'unordered excludes' '
	for_each_ref__exclude refs/heads \
		refs/heads/foo refs/heads/baz >actual 2>perf &&
	for_each_ref refs/heads/bar refs/heads/quux >expect &&

	test_cmp expect actual &&
	case "$GIT_DEFAULT_REF_FORMAT" in
	files)
		assert_jumps 1 perf;;
	reftable)
		assert_jumps 2 perf;;
	*)
		BUG "unhandled ref format $GIT_DEFAULT_REF_FORMAT";;
	esac
'

test_expect_success 'non-matching excluded section' '
	for_each_ref__exclude refs/heads refs/heads/does/not/exist >actual 2>perf &&
	for_each_ref >expect &&

	test_cmp expect actual &&
	assert_no_jumps perf
'

test_expect_success 'meta-characters are discarded' '
	for_each_ref__exclude refs/heads "refs/heads/ba*" >actual 2>perf &&
	for_each_ref >expect &&

	test_cmp expect actual &&
	assert_no_jumps perf
'

test_done
