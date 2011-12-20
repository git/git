#!/bin/sh

test_description="Test whether cache-tree is properly updated

Tests whether various commands properly update and/or rewrite the
cache-tree extension.
"
 . ./test-lib.sh

cmp_cache_tree () {
	test-dump-cache-tree >actual &&
	sed "s/$_x40/SHA/" <actual >filtered &&
	test_cmp "$1" filtered
}

# We don't bother with actually checking the SHA1:
# test-dump-cache-tree already verifies that all existing data is
# correct.
test_shallow_cache_tree () {
	printf "SHA  (%d entries, 0 subtrees)\n" $(git ls-files|wc -l) >expect &&
	cmp_cache_tree expect
}

test_invalid_cache_tree () {
	echo "invalid                                   (0 subtrees)" >expect &&
	printf "SHA #(ref)  (%d entries, 0 subtrees)\n" $(git ls-files|wc -l) >>expect &&
	cmp_cache_tree expect
}

test_no_cache_tree () {
	: >expect &&
	cmp_cache_tree expect
}

test_expect_failure 'initial commit has cache-tree' '
	test_commit foo &&
	test_shallow_cache_tree
'

test_expect_success 'read-tree HEAD establishes cache-tree' '
	git read-tree HEAD &&
	test_shallow_cache_tree
'

test_expect_success 'git-add invalidates cache-tree' '
	test_when_finished "git reset --hard; git read-tree HEAD" &&
	echo "I changed this file" > foo &&
	git add foo &&
	test_invalid_cache_tree
'

test_expect_success 'update-index invalidates cache-tree' '
	test_when_finished "git reset --hard; git read-tree HEAD" &&
	echo "I changed this file" > foo &&
	git update-index --add foo &&
	test_invalid_cache_tree
'

test_expect_success 'write-tree establishes cache-tree' '
	test-scrap-cache-tree &&
	git write-tree &&
	test_shallow_cache_tree
'

test_expect_success 'test-scrap-cache-tree works' '
	git read-tree HEAD &&
	test-scrap-cache-tree &&
	test_no_cache_tree
'

test_expect_success 'second commit has cache-tree' '
	test_commit bar &&
	test_shallow_cache_tree
'

test_expect_success 'reset --hard gives cache-tree' '
	test-scrap-cache-tree &&
	git reset --hard &&
	test_shallow_cache_tree
'

test_expect_success 'reset --hard without index gives cache-tree' '
	rm -f .git/index &&
	git reset --hard &&
	test_shallow_cache_tree
'

test_expect_failure 'checkout gives cache-tree' '
	git checkout HEAD^ &&
	test_shallow_cache_tree
'

test_done
