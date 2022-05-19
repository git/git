#!/bin/sh

test_description="Test whether cache-tree is properly updated

Tests whether various commands properly update and/or rewrite the
cache-tree extension.
"
 . ./test-lib.sh

cmp_cache_tree () {
	test-tool dump-cache-tree | sed -e '/#(ref)/d' >actual &&
	sed "s/$OID_REGEX/SHA/" <actual >filtered &&
	test_cmp "$1" filtered &&
	rm filtered
}

# We don't bother with actually checking the SHA1:
# test-tool dump-cache-tree already verifies that all existing data is
# correct.
generate_expected_cache_tree () {
	pathspec="$1" &&
	dir="$2${2:+/}" &&
	but ls-tree --name-only HEAD -- "$pathspec" >files &&
	but ls-tree --name-only -d HEAD -- "$pathspec" >subtrees &&
	printf "SHA %s (%d entries, %d subtrees)\n" "$dir" $(wc -l <files) $(wc -l <subtrees) &&
	while read subtree
	do
		generate_expected_cache_tree "$pathspec/$subtree/" "$subtree" || return 1
	done <subtrees
}

test_cache_tree () {
	generate_expected_cache_tree "." >expect &&
	cmp_cache_tree expect &&
	rm expect actual files subtrees &&
	but status --porcelain -- ':!status' ':!expected.status' >status &&
	if test -n "$1"
	then
		test_cmp "$1" status
	else
		test_must_be_empty status
	fi
}

test_invalid_cache_tree () {
	printf "invalid                                  %s ()\n" "" "$@" >expect &&
	test-tool dump-cache-tree |
	sed -n -e "s/[0-9]* subtrees//" -e '/#(ref)/d' -e '/^invalid /p' >actual &&
	test_cmp expect actual
}

test_no_cache_tree () {
	>expect &&
	cmp_cache_tree expect
}

test_expect_success 'initial commit has cache-tree' '
	test_cummit foo &&
	test_cache_tree
'

test_expect_success 'read-tree HEAD establishes cache-tree' '
	but read-tree HEAD &&
	test_cache_tree
'

test_expect_success 'but-add invalidates cache-tree' '
	test_when_finished "but reset --hard; but read-tree HEAD" &&
	echo "I changed this file" >foo &&
	but add foo &&
	test_invalid_cache_tree
'

test_expect_success 'but-add in subdir invalidates cache-tree' '
	test_when_finished "but reset --hard; but read-tree HEAD" &&
	mkdir dirx &&
	echo "I changed this file" >dirx/foo &&
	but add dirx/foo &&
	test_invalid_cache_tree
'

test_expect_success 'but-add in subdir does not invalidate sibling cache-tree' '
	but tag no-children &&
	test_when_finished "but reset --hard no-children; but read-tree HEAD" &&
	mkdir dir1 dir2 &&
	test_cummit dir1/a &&
	test_cummit dir2/b &&
	echo "I changed this file" >dir1/a &&
	test_when_finished "rm before" &&
	cat >before <<-\EOF &&
	SHA  (3 entries, 2 subtrees)
	SHA dir1/ (1 entries, 0 subtrees)
	SHA dir2/ (1 entries, 0 subtrees)
	EOF
	cmp_cache_tree before &&
	echo "I changed this file" >dir1/a &&
	but add dir1/a &&
	cat >expect <<-\EOF &&
	invalid                                   (2 subtrees)
	invalid                                  dir1/ (0 subtrees)
	SHA dir2/ (1 entries, 0 subtrees)
	EOF
	cmp_cache_tree expect
'

test_expect_success 'update-index invalidates cache-tree' '
	test_when_finished "but reset --hard; but read-tree HEAD" &&
	echo "I changed this file" >foo &&
	but update-index --add foo &&
	test_invalid_cache_tree
'

test_expect_success 'write-tree establishes cache-tree' '
	test-tool scrap-cache-tree &&
	but write-tree &&
	test_cache_tree
'

test_expect_success 'test-tool scrap-cache-tree works' '
	but read-tree HEAD &&
	test-tool scrap-cache-tree &&
	test_no_cache_tree
'

test_expect_success 'second commit has cache-tree' '
	test_cummit bar &&
	test_cache_tree
'

test_expect_success PERL 'cummit --interactive gives cache-tree on partial cummit' '
	test_when_finished "but reset --hard" &&
	cat <<-\EOT >foo.c &&
	int foo()
	{
		return 42;
	}
	int bar()
	{
		return 42;
	}
	EOT
	but add foo.c &&
	test_invalid_cache_tree &&
	but cummit -m "add a file" &&
	test_cache_tree &&
	cat <<-\EOT >foo.c &&
	int foo()
	{
		return 43;
	}
	int bar()
	{
		return 44;
	}
	EOT
	test_write_lines p 1 "" s n y q |
	but cummit --interactive -m foo &&
	cat <<-\EOF >expected.status &&
	 M foo.c
	EOF
	test_cache_tree expected.status
'

test_expect_success PERL 'cummit -p with shrinking cache-tree' '
	mkdir -p deep/very-long-subdir &&
	echo content >deep/very-long-subdir/file &&
	but add deep &&
	but cummit -m add &&
	but rm -r deep &&

	before=$(wc -c <.but/index) &&
	but cummit -m delete -p &&
	after=$(wc -c <.but/index) &&

	# double check that the index shrank
	test $before -gt $after &&

	# and that our index was not corrupted
	but fsck
'

test_expect_success 'cummit in child dir has cache-tree' '
	mkdir dir &&
	>dir/child.t &&
	but add dir/child.t &&
	but cummit -m dir/child.t &&
	test_cache_tree
'

test_expect_success 'reset --hard gives cache-tree' '
	test-tool scrap-cache-tree &&
	but reset --hard &&
	test_cache_tree
'

test_expect_success 'reset --hard without index gives cache-tree' '
	rm -f .but/index &&
	but clean -fd &&
	but reset --hard &&
	test_cache_tree
'

test_expect_success 'checkout gives cache-tree' '
	but tag current &&
	but checkout HEAD^ &&
	test_cache_tree
'

test_expect_success 'checkout -b gives cache-tree' '
	but checkout current &&
	but checkout -b prev HEAD^ &&
	test_cache_tree
'

test_expect_success 'checkout -B gives cache-tree' '
	but checkout current &&
	but checkout -B prev HEAD^ &&
	test_cache_tree
'

test_expect_success 'merge --ff-only maintains cache-tree' '
	but checkout current &&
	but checkout -b changes &&
	test_cummit llamas &&
	test_cummit pachyderm &&
	test_cache_tree &&
	but checkout current &&
	test_cache_tree &&
	but merge --ff-only changes &&
	test_cache_tree
'

test_expect_success 'merge maintains cache-tree' '
	but checkout current &&
	but checkout -b changes2 &&
	test_cummit alpacas &&
	test_cache_tree &&
	but checkout current &&
	test_cummit struthio &&
	test_cache_tree &&
	but merge changes2 &&
	test_cache_tree
'

test_expect_success 'partial cummit gives cache-tree' '
	but checkout -b partial no-children &&
	test_cummit one &&
	test_cummit two &&
	echo "some change" >one.t &&
	but add one.t &&
	echo "some other change" >two.t &&
	but cummit two.t -m partial &&
	cat <<-\EOF >expected.status &&
	M  one.t
	EOF
	test_cache_tree expected.status
'

test_expect_success 'no phantom error when switching trees' '
	mkdir newdir &&
	>newdir/one &&
	but add newdir/one &&
	but checkout 2>errors &&
	test_must_be_empty errors
'

test_expect_success 'switching trees does not invalidate shared index' '
	(
		sane_unset BUT_TEST_SPLIT_INDEX &&
		but update-index --split-index &&
		>split &&
		but add split &&
		test-tool dump-split-index .but/index | grep -v ^own >before &&
		but cummit -m "as-is" &&
		test-tool dump-split-index .but/index | grep -v ^own >after &&
		test_cmp before after
	)
'

test_done
