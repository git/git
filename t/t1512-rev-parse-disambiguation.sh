#!/bin/sh

test_description='object name disambiguation

Create blobs, trees, commits and a tag that all share the same
prefix, and make sure "git rev-parse" can take advantage of
type information to disambiguate short object names that are
not necessarily unique.

The final history used in the test has five commits, with the bottom
one tagged as v1.0.0.  They all have one regular file each.

  +-------------------------------------------+
  |                                           |
  |           .-------b3wettvi---- ad2uee     |
  |          /                   /            |
  |  a2onsxbvj---czy8f73t--ioiley5o           |
  |                                           |
  +-------------------------------------------+

'

. ./test-lib.sh

if ! test_have_prereq SHA1
then
	skip_all='not using SHA-1 for objects'
	test_done
fi

test_expect_success 'blob and tree' '
	test_tick &&
	(
		for i in 0 1 2 3 4 5 6 7 8 9
		do
			echo $i
		done &&
		echo &&
		echo b1rwzyc3
	) >a0blgqsjc &&

	# create one blob 0000000000b36
	git add a0blgqsjc &&

	# create one tree 0000000000cdc
	git write-tree
'

test_expect_success 'warn ambiguity when no candidate matches type hint' '
	test_must_fail git rev-parse --verify 000000000^{commit} 2>actual &&
	test_i18ngrep "short SHA1 000000000 is ambiguous" actual
'

test_expect_success 'disambiguate tree-ish' '
	# feed tree-ish in an unambiguous way
	git rev-parse --verify 0000000000cdc:a0blgqsjc &&

	# ambiguous at the object name level, but there is only one
	# such tree-ish (the other is a blob)
	git rev-parse --verify 000000000:a0blgqsjc
'

test_expect_success 'disambiguate blob' '
	sed -e "s/|$//" >patch <<-EOF &&
	diff --git a/frotz b/frotz
	index 000000000..ffffff 100644
	--- a/frotz
	+++ b/frotz
	@@ -10,3 +10,4 @@
	 9
	 |
	 b1rwzyc3
	+irwry
	EOF
	(
		GIT_INDEX_FILE=frotz &&
		export GIT_INDEX_FILE &&
		git apply --build-fake-ancestor frotz patch &&
		git cat-file blob :frotz >actual
	) &&
	test_cmp a0blgqsjc actual
'

test_expect_success 'disambiguate tree' '
	commit=$(echo "d7xm" | git commit-tree 000000000) &&
	# this commit is fffff2e and not ambiguous with the 00000* objects
	test $(git rev-parse $commit^{tree}) = $(git rev-parse 0000000000cdc)
'

test_expect_success 'first commit' '
	# create one commit 0000000000e4f
	git commit -m a2onsxbvj
'

test_expect_success 'disambiguate commit-ish' '
	# feed commit-ish in an unambiguous way
	git rev-parse --verify 0000000000e4f^{commit} &&

	# ambiguous at the object name level, but there is only one
	# such commit (the others are tree and blob)
	git rev-parse --verify 000000000^{commit} &&

	# likewise
	git rev-parse --verify 000000000^0
'

test_expect_success 'disambiguate commit' '
	commit=$(echo "hoaxj" | git commit-tree 0000000000cdc -p 000000000) &&
	# this commit is ffffffd8 and not ambiguous with the 00000* objects
	test $(git rev-parse $commit^) = $(git rev-parse 0000000000e4f)
'

test_expect_success 'log name1..name2 takes only commit-ishes on both ends' '
	# These are underspecified from the prefix-length point of view
	# to disambiguate the commit with other objects, but there is only
	# one commit that has 00000* prefix at this point.
	git log 000000000..000000000 &&
	git log ..000000000 &&
	git log 000000000.. &&
	git log 000000000...000000000 &&
	git log ...000000000 &&
	git log 000000000...
'

test_expect_success 'rev-parse name1..name2 takes only commit-ishes on both ends' '
	# Likewise.
	git rev-parse 000000000..000000000 &&
	git rev-parse ..000000000 &&
	git rev-parse 000000000..
'

test_expect_success 'git log takes only commit-ish' '
	# Likewise.
	git log 000000000
'

test_expect_success 'git reset takes only commit-ish' '
	# Likewise.
	git reset 000000000
'

test_expect_success 'first tag' '
	# create one tag 0000000000f8f
	git tag -a -m j7cp83um v1.0.0
'

test_expect_failure 'two semi-ambiguous commit-ish' '
	# At this point, we have a tag 0000000000f8f that points
	# at a commit 0000000000e4f, and a tree and a blob that
	# share 0000000000 prefix with these tag and commit.
	#
	# Once the parser becomes ultra-smart, it could notice that
	# 0000000000 before ^{commit} name many different objects, but
	# that only two (HEAD and v1.0.0 tag) can be peeled to commit,
	# and that peeling them down to commit yield the same commit
	# without ambiguity.
	git rev-parse --verify 0000000000^{commit} &&

	# likewise
	git log 0000000000..0000000000 &&
	git log ..0000000000 &&
	git log 0000000000.. &&
	git log 0000000000...0000000000 &&
	git log ...0000000000 &&
	git log 0000000000...
'

test_expect_failure 'three semi-ambiguous tree-ish' '
	# Likewise for tree-ish.  HEAD, v1.0.0 and HEAD^{tree} share
	# the prefix but peeling them to tree yields the same thing
	git rev-parse --verify 0000000000^{tree}
'

test_expect_success 'parse describe name' '
	# feed an unambiguous describe name
	git rev-parse --verify v1.0.0-0-g0000000000e4f &&

	# ambiguous at the object name level, but there is only one
	# such commit (others are blob, tree and tag)
	git rev-parse --verify v1.0.0-0-g000000000
'

test_expect_success 'more history' '
	# commit 0000000000043
	git mv a0blgqsjc d12cr3h8t &&
	echo h62xsjeu >>d12cr3h8t &&
	git add d12cr3h8t &&

	test_tick &&
	git commit -m czy8f73t &&

	# commit 00000000008ec
	git mv d12cr3h8t j000jmpzn &&
	echo j08bekfvt >>j000jmpzn &&
	git add j000jmpzn &&

	test_tick &&
	git commit -m ioiley5o &&

	# commit 0000000005b0
	git checkout v1.0.0^0 &&
	git mv a0blgqsjc f5518nwu &&

	for i in h62xsjeu j08bekfvt kg7xflhm
	do
		echo $i
	done >>f5518nwu &&
	git add f5518nwu &&

	test_tick &&
	git commit -m b3wettvi &&
	side=$(git rev-parse HEAD) &&

	# commit 000000000066
	git checkout master &&

	# If you use recursive, merge will fail and you will need to
	# clean up a0blgqsjc as well.  If you use resolve, merge will
	# succeed.
	test_might_fail git merge --no-commit -s recursive $side &&
	git rm -f f5518nwu j000jmpzn &&

	test_might_fail git rm -f a0blgqsjc &&
	(
		git cat-file blob $side:f5518nwu &&
		echo j3l0i9s6
	) >ab2gs879 &&
	git add ab2gs879 &&

	test_tick &&
	git commit -m ad2uee

'

test_expect_failure 'parse describe name taking advantage of generation' '
	# ambiguous at the object name level, but there is only one
	# such commit at generation 0
	git rev-parse --verify v1.0.0-0-g000000000 &&

	# likewise for generation 2 and 4
	git rev-parse --verify v1.0.0-2-g000000000 &&
	git rev-parse --verify v1.0.0-4-g000000000
'

# Note: because rev-parse does not even try to disambiguate based on
# the generation number, this test currently succeeds for a wrong
# reason.  When it learns to use the generation number, the previous
# test should succeed, and also this test should fail because the
# describe name used in the test with generation number can name two
# commits.  Make sure that such a future enhancement does not randomly
# pick one.
test_expect_success 'parse describe name not ignoring ambiguity' '
	# ambiguous at the object name level, and there are two such
	# commits at generation 1
	test_must_fail git rev-parse --verify v1.0.0-1-g000000000
'

test_expect_success 'ambiguous commit-ish' '
	# Now there are many commits that begin with the
	# common prefix, none of these should pick one at
	# random.  They all should result in ambiguity errors.
	test_must_fail git rev-parse --verify 00000000^{commit} &&

	# likewise
	test_must_fail git log 000000000..000000000 &&
	test_must_fail git log ..000000000 &&
	test_must_fail git log 000000000.. &&
	test_must_fail git log 000000000...000000000 &&
	test_must_fail git log ...000000000 &&
	test_must_fail git log 000000000...
'

# There are three objects with this prefix: a blob, a tree, and a tag. We know
# the blob will not pass as a treeish, but the tree and tag should (and thus
# cause an error).
test_expect_success 'ambiguous tags peel to treeish' '
	test_must_fail git rev-parse 0000000000f^{tree}
'

test_expect_success 'rev-parse --disambiguate' '
	# The test creates 16 objects that share the prefix and two
	# commits created by commit-tree in earlier tests share a
	# different prefix.
	git rev-parse --disambiguate=000000000 >actual &&
	test_line_count = 16 actual &&
	test "$(sed -e "s/^\(.........\).*/\1/" actual | sort -u)" = 000000000
'

test_expect_success 'rev-parse --disambiguate drops duplicates' '
	git rev-parse --disambiguate=000000000 >expect &&
	git pack-objects .git/objects/pack/pack <expect &&
	git rev-parse --disambiguate=000000000 >actual &&
	test_cmp expect actual
'

test_expect_success 'ambiguous 40-hex ref' '
	TREE=$(git mktree </dev/null) &&
	REF=$(git rev-parse HEAD) &&
	VAL=$(git commit-tree $TREE </dev/null) &&
	git update-ref refs/heads/$REF $VAL &&
	test $(git rev-parse $REF 2>err) = $REF &&
	grep "refname.*${REF}.*ambiguous" err
'

test_expect_success 'ambiguous short sha1 ref' '
	TREE=$(git mktree </dev/null) &&
	REF=$(git rev-parse --short HEAD) &&
	VAL=$(git commit-tree $TREE </dev/null) &&
	git update-ref refs/heads/$REF $VAL &&
	test $(git rev-parse $REF 2>err) = $VAL &&
	grep "refname.*${REF}.*ambiguous" err
'

test_expect_success C_LOCALE_OUTPUT 'ambiguity errors are not repeated (raw)' '
	test_must_fail git rev-parse 00000 2>stderr &&
	grep "is ambiguous" stderr >errors &&
	test_line_count = 1 errors
'

test_expect_success C_LOCALE_OUTPUT 'ambiguity errors are not repeated (treeish)' '
	test_must_fail git rev-parse 00000:foo 2>stderr &&
	grep "is ambiguous" stderr >errors &&
	test_line_count = 1 errors
'

test_expect_success C_LOCALE_OUTPUT 'ambiguity errors are not repeated (peel)' '
	test_must_fail git rev-parse 00000^{commit} 2>stderr &&
	grep "is ambiguous" stderr >errors &&
	test_line_count = 1 errors
'

test_expect_success C_LOCALE_OUTPUT 'ambiguity hints' '
	test_must_fail git rev-parse 000000000 2>stderr &&
	grep ^hint: stderr >hints &&
	# 16 candidates, plus one intro line
	test_line_count = 17 hints
'

test_expect_success C_LOCALE_OUTPUT 'ambiguity hints respect type' '
	test_must_fail git rev-parse 000000000^{commit} 2>stderr &&
	grep ^hint: stderr >hints &&
	# 5 commits, 1 tag (which is a committish), plus intro line
	test_line_count = 7 hints
'

test_expect_success C_LOCALE_OUTPUT 'failed type-selector still shows hint' '
	# these two blobs share the same prefix "ee3d", but neither
	# will pass for a commit
	echo 851 | git hash-object --stdin -w &&
	echo 872 | git hash-object --stdin -w &&
	test_must_fail git rev-parse ee3d^{commit} 2>stderr &&
	grep ^hint: stderr >hints &&
	test_line_count = 3 hints
'

test_expect_success 'core.disambiguate config can prefer types' '
	# ambiguous between tree and tag
	sha1=0000000000f &&
	test_must_fail git rev-parse $sha1 &&
	git rev-parse $sha1^{commit} &&
	git -c core.disambiguate=committish rev-parse $sha1
'

test_expect_success 'core.disambiguate does not override context' '
	# treeish ambiguous between tag and tree
	test_must_fail \
		git -c core.disambiguate=committish rev-parse $sha1^{tree}
'

test_expect_success C_LOCALE_OUTPUT 'ambiguous commits are printed by type first, then hash order' '
	test_must_fail git rev-parse 0000 2>stderr &&
	grep ^hint: stderr >hints &&
	grep 0000 hints >objects &&
	cat >expected <<-\EOF &&
	tag
	commit
	tree
	blob
	EOF
	awk "{print \$3}" <objects >objects.types &&
	uniq <objects.types >objects.types.uniq &&
	test_cmp expected objects.types.uniq &&
	for type in tag commit tree blob
	do
		grep $type objects >$type.objects &&
		sort $type.objects >$type.objects.sorted &&
		test_cmp $type.objects.sorted $type.objects
	done
'

test_expect_success 'cat-file --batch and --batch-check show ambiguous' '
	echo "0000 ambiguous" >expect &&
	echo 0000 | git cat-file --batch-check >actual 2>err &&
	test_cmp expect actual &&
	test_i18ngrep hint: err &&
	echo 0000 | git cat-file --batch >actual 2>err &&
	test_cmp expect actual &&
	test_i18ngrep hint: err
'

test_done
