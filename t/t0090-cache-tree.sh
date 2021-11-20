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
	git ls-tree --name-only HEAD -- "$pathspec" >files &&
	git ls-tree --name-only -d HEAD -- "$pathspec" >subtrees &&
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
	git status --porcelain -- ':!status' ':!expected.status' >status &&
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
	test_commit foo &&
	test_cache_tree
'

test_expect_success 'read-tree HEAD establishes cache-tree' '
	git read-tree HEAD &&
	test_cache_tree
'

test_expect_success 'git-add invalidates cache-tree' '
	test_when_finished "git reset --hard; git read-tree HEAD" &&
	echo "I changed this file" >foo &&
	git add foo &&
	test_invalid_cache_tree
'

test_expect_success 'git-add in subdir invalidates cache-tree' '
	test_when_finished "git reset --hard; git read-tree HEAD" &&
	mkdir dirx &&
	echo "I changed this file" >dirx/foo &&
	git add dirx/foo &&
	test_invalid_cache_tree
'

test_expect_success 'git-add in subdir does not invalidate sibling cache-tree' '
	git tag no-children &&
	test_when_finished "git reset --hard no-children; git read-tree HEAD" &&
	mkdir dir1 dir2 &&
	test_commit dir1/a &&
	test_commit dir2/b &&
	echo "I changed this file" >dir1/a &&
	test_when_finished "rm before" &&
	cat >before <<-\EOF &&
	SHA  (3 entries, 2 subtrees)
	SHA dir1/ (1 entries, 0 subtrees)
	SHA dir2/ (1 entries, 0 subtrees)
	EOF
	cmp_cache_tree before &&
	echo "I changed this file" >dir1/a &&
	git add dir1/a &&
	cat >expect <<-\EOF &&
	invalid                                   (2 subtrees)
	invalid                                  dir1/ (0 subtrees)
	SHA dir2/ (1 entries, 0 subtrees)
	EOF
	cmp_cache_tree expect
'

test_expect_success 'update-index invalidates cache-tree' '
	test_when_finished "git reset --hard; git read-tree HEAD" &&
	echo "I changed this file" >foo &&
	git update-index --add foo &&
	test_invalid_cache_tree
'

test_expect_success 'write-tree establishes cache-tree' '
	test-tool scrap-cache-tree &&
	git write-tree &&
	test_cache_tree
'

test_expect_success 'test-tool scrap-cache-tree works' '
	git read-tree HEAD &&
	test-tool scrap-cache-tree &&
	test_no_cache_tree
'

test_expect_success 'second commit has cache-tree' '
	test_commit bar &&
	test_cache_tree
'

test_expect_success PERL 'commit --interactive gives cache-tree on partial commit' '
	test_when_finished "git reset --hard" &&
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
	git add foo.c &&
	test_invalid_cache_tree &&
	git commit -m "add a file" &&
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
	git commit --interactive -m foo &&
	cat <<-\EOF >expected.status &&
	 M foo.c
	EOF
	test_cache_tree expected.status
'

test_expect_success PERL 'commit -p with shrinking cache-tree' '
	mkdir -p deep/very-long-subdir &&
	echo content >deep/very-long-subdir/file &&
	git add deep &&
	git commit -m add &&
	git rm -r deep &&

	before=$(wc -c <.git/index) &&
	git commit -m delete -p &&
	after=$(wc -c <.git/index) &&

	# double check that the index shrank
	test $before -gt $after &&

	# and that our index was not corrupted
	git fsck
'

test_expect_success 'commit in child dir has cache-tree' '
	mkdir dir &&
	>dir/child.t &&
	git add dir/child.t &&
	git commit -m dir/child.t &&
	test_cache_tree
'

test_expect_success 'reset --hard gives cache-tree' '
	test-tool scrap-cache-tree &&
	git reset --hard &&
	test_cache_tree
'

test_expect_success 'reset --hard without index gives cache-tree' '
	rm -f .git/index &&
	git clean -fd &&
	git reset --hard &&
	test_cache_tree
'

test_expect_success 'checkout gives cache-tree' '
	git tag current &&
	git checkout HEAD^ &&
	test_cache_tree
'

test_expect_success 'checkout -b gives cache-tree' '
	git checkout current &&
	git checkout -b prev HEAD^ &&
	test_cache_tree
'

test_expect_success 'checkout -B gives cache-tree' '
	git checkout current &&
	git checkout -B prev HEAD^ &&
	test_cache_tree
'

test_expect_success 'merge --ff-only maintains cache-tree' '
	git checkout current &&
	git checkout -b changes &&
	test_commit llamas &&
	test_commit pachyderm &&
	test_cache_tree &&
	git checkout current &&
	test_cache_tree &&
	git merge --ff-only changes &&
	test_cache_tree
'

test_expect_success 'merge maintains cache-tree' '
	git checkout current &&
	git checkout -b changes2 &&
	test_commit alpacas &&
	test_cache_tree &&
	git checkout current &&
	test_commit struthio &&
	test_cache_tree &&
	git merge changes2 &&
	test_cache_tree
'

test_expect_success 'partial commit gives cache-tree' '
	git checkout -b partial no-children &&
	test_commit one &&
	test_commit two &&
	echo "some change" >one.t &&
	git add one.t &&
	echo "some other change" >two.t &&
	git commit two.t -m partial &&
	cat <<-\EOF >expected.status &&
	M  one.t
	EOF
	test_cache_tree expected.status
'

test_expect_success 'no phantom error when switching trees' '
	mkdir newdir &&
	>newdir/one &&
	git add newdir/one &&
	git checkout 2>errors &&
	test_must_be_empty errors
'

test_expect_success 'switching trees does not invalidate shared index' '
	(
		sane_unset GIT_TEST_SPLIT_INDEX &&
		git update-index --split-index &&
		>split &&
		git add split &&
		test-tool dump-split-index .git/index | grep -v ^own >before &&
		git commit -m "as-is" &&
		test-tool dump-split-index .git/index | grep -v ^own >after &&
		test_cmp before after
	)
'

test_done
