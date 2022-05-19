#!/bin/sh

test_description='tests for the peel_ref optimization of packed-refs'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create annotated tag in refs/tags' '
	test_cummit base &&
	but tag -m annotated foo
'

test_expect_success 'create annotated tag outside of refs/tags' '
	but update-ref refs/outside/foo refs/tags/foo
'

# This matches show-ref's output
print_ref() {
	echo "$(but rev-parse "$1") $1"
}

test_expect_success 'set up expected show-ref output' '
	{
		print_ref "refs/heads/main" &&
		print_ref "refs/outside/foo" &&
		print_ref "refs/outside/foo^{}" &&
		print_ref "refs/tags/base" &&
		print_ref "refs/tags/foo" &&
		print_ref "refs/tags/foo^{}"
	} >expect
'

test_expect_success 'refs are peeled outside of refs/tags (loose)' '
	but show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'refs are peeled outside of refs/tags (packed)' '
	but pack-refs --all &&
	but show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'create old-style pack-refs without fully-peeled' '
	# Git no longer writes without fully-peeled, so we just write our own
	# from scratch; we could also munge the existing file to remove the
	# fully-peeled bits, but that seems even more prone to failure,
	# especially if the format ever changes again. At least this way we
	# know we are emulating exactly what an older but would have written.
	{
		echo "# pack-refs with: peeled " &&
		print_ref "refs/heads/main" &&
		print_ref "refs/outside/foo" &&
		print_ref "refs/tags/base" &&
		print_ref "refs/tags/foo" &&
		echo "^$(but rev-parse "refs/tags/foo^{}")"
	} >tmp &&
	mv tmp .but/packed-refs
'

test_expect_success 'refs are peeled outside of refs/tags (old packed)' '
	but show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'peeled refs survive deletion of packed ref' '
	but pack-refs --all &&
	cp .but/packed-refs fully-peeled &&
	but branch yadda &&
	but pack-refs --all &&
	but branch -d yadda &&
	test_cmp fully-peeled .but/packed-refs
'

test_done
