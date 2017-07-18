#!/bin/sh

test_description='tests for the peel_ref optimization of packed-refs'
. ./test-lib.sh

test_expect_success 'create annotated tag in refs/tags' '
	test_commit base &&
	git tag -m annotated foo
'

test_expect_success 'create annotated tag outside of refs/tags' '
	git update-ref refs/outside/foo refs/tags/foo
'

# This matches show-ref's output
print_ref() {
	echo "$(git rev-parse "$1") $1"
}

test_expect_success 'set up expected show-ref output' '
	{
		print_ref "refs/heads/master" &&
		print_ref "refs/outside/foo" &&
		print_ref "refs/outside/foo^{}" &&
		print_ref "refs/tags/base" &&
		print_ref "refs/tags/foo" &&
		print_ref "refs/tags/foo^{}"
	} >expect
'

test_expect_success 'refs are peeled outside of refs/tags (loose)' '
	git show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'refs are peeled outside of refs/tags (packed)' '
	git pack-refs --all &&
	git show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'create old-style pack-refs without fully-peeled' '
	# Git no longer writes without fully-peeled, so we just write our own
	# from scratch; we could also munge the existing file to remove the
	# fully-peeled bits, but that seems even more prone to failure,
	# especially if the format ever changes again. At least this way we
	# know we are emulating exactly what an older git would have written.
	{
		echo "# pack-refs with: peeled " &&
		print_ref "refs/heads/master" &&
		print_ref "refs/outside/foo" &&
		print_ref "refs/tags/base" &&
		print_ref "refs/tags/foo" &&
		echo "^$(git rev-parse "refs/tags/foo^{}")"
	} >tmp &&
	mv tmp .git/packed-refs
'

test_expect_success 'refs are peeled outside of refs/tags (old packed)' '
	git show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'peeled refs survive deletion of packed ref' '
	git pack-refs --all &&
	cp .git/packed-refs fully-peeled &&
	git branch yadda &&
	git pack-refs --all &&
	git branch -d yadda &&
	test_cmp fully-peeled .git/packed-refs
'

test_done
