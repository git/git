#!/bin/sh

test_description='object name disambiguation

Create blobs, trees, cummits and a tag that all share the same
prefix, and make sure "but rev-parse" can take advantage of
type information to disambiguate short object names that are
not necessarily unique.

The final history used in the test has five cummits, with the bottom
one tagged as v1.0.0.  They all have one regular file each.

  +-------------------------------------------+
  |                                           |
  |           .-------b3wettvi---- ad2uee     |
  |          /                   /            |
  |  a2onsxbvj---czy8f73t--ioiley5o           |
  |                                           |
  +-------------------------------------------+

'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_cmp_failed_rev_parse () {
	dir=$1
	rev=$2

	cat >expect &&
	test_must_fail but -C "$dir" rev-parse "$rev" 2>actual.raw &&
	sed "s/\($rev\)[0-9a-f]*/\1.../" <actual.raw >actual &&
	test_cmp expect actual
}

test_expect_success 'ambiguous blob output' '
	but init --bare blob.prefix &&
	(
		cd blob.prefix &&

		# Both start with "dead..", under both SHA-1 and SHA-256
		echo brocdnra | but hash-object -w --stdin &&
		echo brigddsv | but hash-object -w --stdin &&

		# Both start with "beef.."
		echo 1agllotbh | but hash-object -w --stdin &&
		echo 1bbfctrkc | but hash-object -w --stdin
	) &&

	test_must_fail but -C blob.prefix rev-parse dead &&
	test_cmp_failed_rev_parse blob.prefix beef <<-\EOF
	error: short object ID beef... is ambiguous
	hint: The candidates are:
	hint:   beef... blob
	hint:   beef... blob
	fatal: ambiguous argument '\''beef...'\'': unknown revision or path not in the working tree.
	Use '\''--'\'' to separate paths from revisions, like this:
	'\''but <command> [<revision>...] -- [<file>...]'\''
	EOF
'

test_expect_success 'ambiguous loose bad object parsed as OBJ_BAD' '
	but init --bare blob.bad &&
	(
		cd blob.bad &&

		# Both have the prefix "bad0"
		echo xyzfaowcoh | but hash-object -t bad -w --stdin --literally &&
		echo xyzhjpyvwl | but hash-object -t bad -w --stdin --literally
	) &&

	test_cmp_failed_rev_parse blob.bad bad0 <<-\EOF
	error: short object ID bad0... is ambiguous
	fatal: invalid object type
	EOF
'

test_expect_success POSIXPERM 'ambigous zlib corrupt loose blob' '
	but init --bare blob.corrupt &&
	(
		cd blob.corrupt &&

		# Both have the prefix "cafe"
		echo bnkxmdwz | but hash-object -w --stdin &&
		oid=$(echo bmwsjxzi | but hash-object -w --stdin) &&

		oidf=objects/$(test_oid_to_path "$oid") &&
		chmod 755 $oidf &&
		echo broken >$oidf
	) &&

	test_cmp_failed_rev_parse blob.corrupt cafe <<-\EOF
	error: short object ID cafe... is ambiguous
	error: inflate: data stream error (incorrect header check)
	error: unable to unpack cafe... header
	error: inflate: data stream error (incorrect header check)
	error: unable to unpack cafe... header
	hint: The candidates are:
	hint:   cafe... [bad object]
	hint:   cafe... blob
	fatal: ambiguous argument '\''cafe...'\'': unknown revision or path not in the working tree.
	Use '\''--'\'' to separate paths from revisions, like this:
	'\''but <command> [<revision>...] -- [<file>...]'\''
	EOF
'

if ! test_have_prereq SHA1
then
	skip_all='not using SHA-1 for objects'
	test_done
fi

test_expect_success 'blob and tree' '
	test_tick &&
	(
		test_write_lines 0 1 2 3 4 5 6 7 8 9 &&
		echo &&
		echo b1rwzyc3
	) >a0blgqsjc &&

	# create one blob 0000000000b36
	but add a0blgqsjc &&

	# create one tree 0000000000cdc
	but write-tree
'

test_expect_success 'warn ambiguity when no candidate matches type hint' '
	test_must_fail but rev-parse --verify 000000000^{cummit} 2>actual &&
	test_i18ngrep "short object ID 000000000 is ambiguous" actual
'

test_expect_success 'disambiguate tree-ish' '
	# feed tree-ish in an unambiguous way
	but rev-parse --verify 0000000000cdc:a0blgqsjc &&

	# ambiguous at the object name level, but there is only one
	# such tree-ish (the other is a blob)
	but rev-parse --verify 000000000:a0blgqsjc
'

test_expect_success 'disambiguate blob' '
	sed -e "s/|$//" >patch <<-EOF &&
	diff --but a/frotz b/frotz
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
		but apply --build-fake-ancestor frotz patch &&
		but cat-file blob :frotz >actual
	) &&
	test_cmp a0blgqsjc actual
'

test_expect_success 'disambiguate tree' '
	cummit=$(echo "d7xm" | but cummit-tree 000000000) &&
	# this cummit is fffff2e and not ambiguous with the 00000* objects
	test $(but rev-parse $cummit^{tree}) = $(but rev-parse 0000000000cdc)
'

test_expect_success 'first cummit' '
	# create one cummit 0000000000e4f
	but cummit -m a2onsxbvj
'

test_expect_success 'disambiguate cummit-ish' '
	# feed cummit-ish in an unambiguous way
	but rev-parse --verify 0000000000e4f^{cummit} &&

	# ambiguous at the object name level, but there is only one
	# such cummit (the others are tree and blob)
	but rev-parse --verify 000000000^{cummit} &&

	# likewise
	but rev-parse --verify 000000000^0
'

test_expect_success 'disambiguate cummit' '
	cummit=$(echo "hoaxj" | but cummit-tree 0000000000cdc -p 000000000) &&
	# this cummit is ffffffd8 and not ambiguous with the 00000* objects
	test $(but rev-parse $cummit^) = $(but rev-parse 0000000000e4f)
'

test_expect_success 'log name1..name2 takes only cummit-ishes on both ends' '
	# These are underspecified from the prefix-length point of view
	# to disambiguate the cummit with other objects, but there is only
	# one cummit that has 00000* prefix at this point.
	but log 000000000..000000000 &&
	but log ..000000000 &&
	but log 000000000.. &&
	but log 000000000...000000000 &&
	but log ...000000000 &&
	but log 000000000...
'

test_expect_success 'rev-parse name1..name2 takes only cummit-ishes on both ends' '
	# Likewise.
	but rev-parse 000000000..000000000 &&
	but rev-parse ..000000000 &&
	but rev-parse 000000000..
'

test_expect_success 'but log takes only cummit-ish' '
	# Likewise.
	but log 000000000
'

test_expect_success 'but reset takes only cummit-ish' '
	# Likewise.
	but reset 000000000
'

test_expect_success 'first tag' '
	# create one tag 0000000000f8f
	but tag -a -m j7cp83um v1.0.0
'

test_expect_failure 'two semi-ambiguous cummit-ish' '
	# At this point, we have a tag 0000000000f8f that points
	# at a cummit 0000000000e4f, and a tree and a blob that
	# share 0000000000 prefix with these tag and cummit.
	#
	# Once the parser becomes ultra-smart, it could notice that
	# 0000000000 before ^{cummit} name many different objects, but
	# that only two (HEAD and v1.0.0 tag) can be peeled to cummit,
	# and that peeling them down to cummit yield the same cummit
	# without ambiguity.
	but rev-parse --verify 0000000000^{cummit} &&

	# likewise
	but log 0000000000..0000000000 &&
	but log ..0000000000 &&
	but log 0000000000.. &&
	but log 0000000000...0000000000 &&
	but log ...0000000000 &&
	but log 0000000000...
'

test_expect_failure 'three semi-ambiguous tree-ish' '
	# Likewise for tree-ish.  HEAD, v1.0.0 and HEAD^{tree} share
	# the prefix but peeling them to tree yields the same thing
	but rev-parse --verify 0000000000^{tree}
'

test_expect_success 'parse describe name' '
	# feed an unambiguous describe name
	but rev-parse --verify v1.0.0-0-g0000000000e4f &&

	# ambiguous at the object name level, but there is only one
	# such cummit (others are blob, tree and tag)
	but rev-parse --verify v1.0.0-0-g000000000
'

test_expect_success 'more history' '
	# cummit 0000000000043
	but mv a0blgqsjc d12cr3h8t &&
	echo h62xsjeu >>d12cr3h8t &&
	but add d12cr3h8t &&

	test_tick &&
	but cummit -m czy8f73t &&

	# cummit 00000000008ec
	but mv d12cr3h8t j000jmpzn &&
	echo j08bekfvt >>j000jmpzn &&
	but add j000jmpzn &&

	test_tick &&
	but cummit -m ioiley5o &&

	# cummit 0000000005b0
	but checkout v1.0.0^0 &&
	but mv a0blgqsjc f5518nwu &&

	test_write_lines h62xsjeu j08bekfvt kg7xflhm >>f5518nwu &&
	but add f5518nwu &&

	test_tick &&
	but cummit -m b3wettvi &&
	side=$(but rev-parse HEAD) &&

	# cummit 000000000066
	but checkout main &&

	# If you use recursive, merge will fail and you will need to
	# clean up a0blgqsjc as well.  If you use resolve, merge will
	# succeed.
	test_might_fail but merge --no-cummit -s recursive $side &&
	but rm -f f5518nwu j000jmpzn &&

	test_might_fail but rm -f a0blgqsjc &&
	(
		but cat-file blob $side:f5518nwu &&
		echo j3l0i9s6
	) >ab2gs879 &&
	but add ab2gs879 &&

	test_tick &&
	but cummit -m ad2uee

'

test_expect_failure 'parse describe name taking advantage of generation' '
	# ambiguous at the object name level, but there is only one
	# such cummit at generation 0
	but rev-parse --verify v1.0.0-0-g000000000 &&

	# likewise for generation 2 and 4
	but rev-parse --verify v1.0.0-2-g000000000 &&
	but rev-parse --verify v1.0.0-4-g000000000
'

# Note: because rev-parse does not even try to disambiguate based on
# the generation number, this test currently succeeds for a wrong
# reason.  When it learns to use the generation number, the previous
# test should succeed, and also this test should fail because the
# describe name used in the test with generation number can name two
# cummits.  Make sure that such a future enhancement does not randomly
# pick one.
test_expect_success 'parse describe name not ignoring ambiguity' '
	# ambiguous at the object name level, and there are two such
	# cummits at generation 1
	test_must_fail but rev-parse --verify v1.0.0-1-g000000000
'

test_expect_success 'ambiguous cummit-ish' '
	# Now there are many cummits that begin with the
	# common prefix, none of these should pick one at
	# random.  They all should result in ambiguity errors.
	test_must_fail but rev-parse --verify 00000000^{cummit} &&

	# likewise
	test_must_fail but log 000000000..000000000 &&
	test_must_fail but log ..000000000 &&
	test_must_fail but log 000000000.. &&
	test_must_fail but log 000000000...000000000 &&
	test_must_fail but log ...000000000 &&
	test_must_fail but log 000000000...
'

# There are three objects with this prefix: a blob, a tree, and a tag. We know
# the blob will not pass as a treeish, but the tree and tag should (and thus
# cause an error).
test_expect_success 'ambiguous tags peel to treeish' '
	test_must_fail but rev-parse 0000000000f^{tree}
'

test_expect_success 'rev-parse --disambiguate' '
	# The test creates 16 objects that share the prefix and two
	# cummits created by cummit-tree in earlier tests share a
	# different prefix.
	but rev-parse --disambiguate=000000000 >actual &&
	test_line_count = 16 actual &&
	test "$(sed -e "s/^\(.........\).*/\1/" actual | sort -u)" = 000000000
'

test_expect_success 'rev-parse --disambiguate drops duplicates' '
	but rev-parse --disambiguate=000000000 >expect &&
	but pack-objects .but/objects/pack/pack <expect &&
	but rev-parse --disambiguate=000000000 >actual &&
	test_cmp expect actual
'

test_expect_success 'ambiguous 40-hex ref' '
	TREE=$(but mktree </dev/null) &&
	REF=$(but rev-parse HEAD) &&
	VAL=$(but cummit-tree $TREE </dev/null) &&
	but update-ref refs/heads/$REF $VAL &&
	test $(but rev-parse $REF 2>err) = $REF &&
	grep "refname.*${REF}.*ambiguous" err
'

test_expect_success 'ambiguous short sha1 ref' '
	TREE=$(but mktree </dev/null) &&
	REF=$(but rev-parse --short HEAD) &&
	VAL=$(but cummit-tree $TREE </dev/null) &&
	but update-ref refs/heads/$REF $VAL &&
	test $(but rev-parse $REF 2>err) = $VAL &&
	grep "refname.*${REF}.*ambiguous" err
'

test_expect_success 'ambiguity errors are not repeated (raw)' '
	test_must_fail but rev-parse 00000 2>stderr &&
	grep "is ambiguous" stderr >errors &&
	test_line_count = 1 errors
'

test_expect_success 'ambiguity errors are not repeated (treeish)' '
	test_must_fail but rev-parse 00000:foo 2>stderr &&
	grep "is ambiguous" stderr >errors &&
	test_line_count = 1 errors
'

test_expect_success 'ambiguity errors are not repeated (peel)' '
	test_must_fail but rev-parse 00000^{cummit} 2>stderr &&
	grep "is ambiguous" stderr >errors &&
	test_line_count = 1 errors
'

test_expect_success 'ambiguity hints' '
	test_must_fail but rev-parse 000000000 2>stderr &&
	grep ^hint: stderr >hints &&
	# 16 candidates, plus one intro line
	test_line_count = 17 hints
'

test_expect_success 'ambiguity hints respect type' '
	test_must_fail but rev-parse 000000000^{cummit} 2>stderr &&
	grep ^hint: stderr >hints &&
	# 5 cummits, 1 tag (which is a cummittish), plus intro line
	test_line_count = 7 hints
'

test_expect_success 'failed type-selector still shows hint' '
	# these two blobs share the same prefix "ee3d", but neither
	# will pass for a cummit
	echo 851 | but hash-object --stdin -w &&
	echo 872 | but hash-object --stdin -w &&
	test_must_fail but rev-parse ee3d^{cummit} 2>stderr &&
	grep ^hint: stderr >hints &&
	test_line_count = 3 hints
'

test_expect_success 'core.disambiguate config can prefer types' '
	# ambiguous between tree and tag
	sha1=0000000000f &&
	test_must_fail but rev-parse $sha1 &&
	but rev-parse $sha1^{cummit} &&
	but -c core.disambiguate=cummittish rev-parse $sha1
'

test_expect_success 'core.disambiguate does not override context' '
	# treeish ambiguous between tag and tree
	test_must_fail \
		but -c core.disambiguate=cummittish rev-parse $sha1^{tree}
'

test_expect_success 'ambiguous cummits are printed by type first, then hash order' '
	test_must_fail but rev-parse 0000 2>stderr &&
	grep ^hint: stderr >hints &&
	grep 0000 hints >objects &&
	cat >expected <<-\EOF &&
	tag
	cummit
	tree
	blob
	EOF
	awk "{print \$3}" <objects >objects.types &&
	uniq <objects.types >objects.types.uniq &&
	test_cmp expected objects.types.uniq &&
	for type in tag cummit tree blob
	do
		grep $type objects >$type.objects &&
		sort $type.objects >$type.objects.sorted &&
		test_cmp $type.objects.sorted $type.objects || return 1
	done
'

test_expect_success 'cat-file --batch and --batch-check show ambiguous' '
	echo "0000 ambiguous" >expect &&
	echo 0000 | but cat-file --batch-check >actual 2>err &&
	test_cmp expect actual &&
	test_i18ngrep hint: err &&
	echo 0000 | but cat-file --batch >actual 2>err &&
	test_cmp expect actual &&
	test_i18ngrep hint: err
'

test_done
