#!/bin/sh

test_description='test describe'

#  o---o-----o----o----o-------o----x
#       \   D,R   e           /
#        \---o-------------o-'
#         \  B            /
#          `-o----o----o-'
#                 A    c
#
# First parent of a merge cummit is on the same line, second parent below.

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_describe () {
	indir= &&
	while test $# != 0
	do
		case "$1" in
		-C)
			indir="$2"
			shift
			;;
		*)
			break
			;;
		esac
		shift
	done &&
	indir=${indir:+"$indir"/} &&
	expect="$1"
	shift
	describe_opts="$@"
	test_expect_success "describe $describe_opts" '
		but ${indir:+ -C "$indir"} describe $describe_opts >raw &&
		sed -e "s/-g[0-9a-f]*\$/-gHASH/" <raw >actual &&
		echo "$expect" >expect &&
		test_cmp expect actual
	'
}

test_expect_success setup '
	test_cummit initial file one &&
	test_cummit second file two &&
	test_cummit third file three &&
	test_cummit --annotate A file A &&
	test_cummit c file c &&

	but reset --hard second &&
	test_cummit --annotate B side B &&

	test_tick &&
	but merge -m Merged c &&
	merged=$(but rev-parse HEAD) &&

	but reset --hard second &&
	test_cummit --no-tag D another D &&

	test_tick &&
	but tag -a -m R R &&

	test_cummit e another DD &&
	test_cummit --no-tag "yet another" another DDD &&

	test_tick &&
	but merge -m Merged $merged &&

	test_cummit --no-tag x file
'

check_describe A-8-gHASH HEAD
check_describe A-7-gHASH HEAD^
check_describe R-2-gHASH HEAD^^
check_describe A-3-gHASH HEAD^^2
check_describe B HEAD^^2^
check_describe R-1-gHASH HEAD^^^

check_describe c-7-gHASH --tags HEAD
check_describe c-6-gHASH --tags HEAD^
check_describe e-1-gHASH --tags HEAD^^
check_describe c-2-gHASH --tags HEAD^^2
check_describe B --tags HEAD^^2^
check_describe e --tags HEAD^^^

check_describe heads/main --all HEAD
check_describe tags/c-6-gHASH --all HEAD^
check_describe tags/e --all HEAD^^^

check_describe B-0-gHASH --long HEAD^^2^
check_describe A-3-gHASH --long HEAD^^2

check_describe c-7-gHASH --tags
check_describe e-3-gHASH --first-parent --tags

test_expect_success 'describe --contains defaults to HEAD without cummit-ish' '
	echo "A^0" >expect &&
	but checkout A &&
	test_when_finished "but checkout -" &&
	but describe --contains >actual &&
	test_cmp expect actual
'

check_describe tags/A --all A^0

test_expect_success 'renaming tag A to Q locally produces a warning' "
	but update-ref refs/tags/Q $(but rev-parse refs/tags/A) &&
	but update-ref -d refs/tags/A &&
	but describe HEAD 2>err >out &&
	cat >expected <<-\EOF &&
	warning: tag 'Q' is externally known as 'A'
	EOF
	test_cmp expected err &&
	grep -E '^A-8-g[0-9a-f]+$' out
"

test_expect_success 'misnamed annotated tag forces long output' '
	description=$(but describe --no-long Q^0) &&
	expr "$description" : "A-0-g[0-9a-f]*$" &&
	but rev-parse --verify "$description" >actual &&
	but rev-parse --verify Q^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'abbrev=0 will not break misplaced tag (1)' '
	description=$(but describe --abbrev=0 Q^0) &&
	expr "$description" : "A-0-g[0-9a-f]*$"
'

test_expect_success 'abbrev=0 will not break misplaced tag (2)' '
	description=$(but describe --abbrev=0 c^0) &&
	expr "$description" : "A-1-g[0-9a-f]*$"
'

test_expect_success 'rename tag Q back to A' '
	but update-ref refs/tags/A $(but rev-parse refs/tags/Q) &&
	but update-ref -d refs/tags/Q
'

test_expect_success 'pack tag refs' 'but pack-refs'
check_describe A-8-gHASH HEAD

test_expect_success 'describe works from outside repo using --but-dir' '
	but clone --bare "$TRASH_DIRECTORY" "$TRASH_DIRECTORY/bare" &&
	but --but-dir "$TRASH_DIRECTORY/bare" describe >out &&
	grep -E "^A-8-g[0-9a-f]+$" out
'

check_describe "A-8-gHASH" --dirty

test_expect_success 'describe --dirty with --work-tree' '
	(
		cd "$TEST_DIRECTORY" &&
		but --but-dir "$TRASH_DIRECTORY/.but" --work-tree "$TRASH_DIRECTORY" describe --dirty >"$TRASH_DIRECTORY/out"
	) &&
	grep -E "^A-8-g[0-9a-f]+$" out
'

test_expect_success 'set-up dirty work tree' '
	echo >>file
'

test_expect_success 'describe --dirty with --work-tree (dirty)' '
	but describe --dirty >expected &&
	(
		cd "$TEST_DIRECTORY" &&
		but --but-dir "$TRASH_DIRECTORY/.but" --work-tree "$TRASH_DIRECTORY" describe --dirty >"$TRASH_DIRECTORY/out"
	) &&
	grep -E "^A-8-g[0-9a-f]+-dirty$" out &&
	test_cmp expected out
'

test_expect_success 'describe --dirty=.mod with --work-tree (dirty)' '
	but describe --dirty=.mod >expected &&
	(
		cd "$TEST_DIRECTORY" &&
		but --but-dir "$TRASH_DIRECTORY/.but" --work-tree "$TRASH_DIRECTORY" describe --dirty=.mod >"$TRASH_DIRECTORY/out"
	) &&
	grep -E "^A-8-g[0-9a-f]+.mod$" out &&
	test_cmp expected out
'

test_expect_success 'describe --dirty HEAD' '
	test_must_fail but describe --dirty HEAD
'

test_expect_success 'set-up matching pattern tests' '
	but tag -a -m test-annotated test-annotated &&
	echo >>file &&
	test_tick &&
	but cummit -a -m "one more" &&
	but tag test1-lightweight &&
	echo >>file &&
	test_tick &&
	but cummit -a -m "yet another" &&
	but tag test2-lightweight &&
	echo >>file &&
	test_tick &&
	but cummit -a -m "even more"

'

check_describe "test-annotated-3-gHASH" --match="test-*"

check_describe "test1-lightweight-2-gHASH" --tags --match="test1-*"

check_describe "test2-lightweight-1-gHASH" --tags --match="test2-*"

check_describe "test2-lightweight-0-gHASH" --long --tags --match="test2-*" HEAD^

check_describe "test2-lightweight-0-gHASH" --long --tags --match="test1-*" --match="test2-*" HEAD^

check_describe "test2-lightweight-0-gHASH" --long --tags --match="test1-*" --no-match --match="test2-*" HEAD^

check_describe "test1-lightweight-2-gHASH" --long --tags --match="test1-*" --match="test3-*" HEAD

check_describe "test1-lightweight-2-gHASH" --long --tags --match="test3-*" --match="test1-*" HEAD

test_expect_success 'set-up branches' '
	but branch branch_A A &&
	but branch branch_C c &&
	but update-ref refs/remotes/origin/remote_branch_A "A^{cummit}" &&
	but update-ref refs/remotes/origin/remote_branch_C "c^{cummit}" &&
	but update-ref refs/original/original_branch_A test-annotated~2
'

check_describe "heads/branch_A-11-gHASH" --all --match="branch_*" --exclude="branch_C" HEAD

check_describe "remotes/origin/remote_branch_A-11-gHASH" --all --match="origin/remote_branch_*" --exclude="origin/remote_branch_C" HEAD

check_describe "original/original_branch_A-6-gHASH" --all test-annotated~1

test_expect_success '--match does not work for other types' '
	test_must_fail but describe --all --match="*original_branch_*" test-annotated~1
'

test_expect_success '--exclude does not work for other types' '
	R=$(but describe --all --exclude="any_pattern_even_not_matching" test-annotated~1) &&
	case "$R" in
	*original_branch_A*) echo "fail: Found unknown reference $R with --exclude"
		false;;
	*) echo ok: Found some known type;;
	esac
'

test_expect_success 'name-rev with exact tags' '
	echo A >expect &&
	tag_object=$(but rev-parse refs/tags/A) &&
	but name-rev --tags --name-only $tag_object >actual &&
	test_cmp expect actual &&

	echo "A^0" >expect &&
	tagged_cummit=$(but rev-parse "refs/tags/A^0") &&
	but name-rev --tags --name-only $tagged_cummit >actual &&
	test_cmp expect actual
'

test_expect_success 'name-rev --all' '
	>expect.unsorted &&
	for rev in $(but rev-list --all)
	do
		but name-rev $rev >>expect.unsorted || return 1
	done &&
	sort <expect.unsorted >expect &&
	but name-rev --all >actual.unsorted &&
	sort <actual.unsorted >actual &&
	test_cmp expect actual
'

test_expect_success 'name-rev --annotate-stdin' '
	>expect.unsorted &&
	for rev in $(but rev-list --all)
	do
		name=$(but name-rev --name-only $rev) &&
		echo "$rev ($name)" >>expect.unsorted || return 1
	done &&
	sort <expect.unsorted >expect &&
	but rev-list --all | but name-rev --annotate-stdin >actual.unsorted &&
	sort <actual.unsorted >actual &&
	test_cmp expect actual
'

test_expect_success 'name-rev --stdin deprecated' "
	but rev-list --all | but name-rev --stdin 2>actual &&
	grep -E 'warning: --stdin is deprecated' actual
"

test_expect_success 'describe --contains with the exact tags' '
	echo "A^0" >expect &&
	tag_object=$(but rev-parse refs/tags/A) &&
	but describe --contains $tag_object >actual &&
	test_cmp expect actual &&

	echo "A^0" >expect &&
	tagged_cummit=$(but rev-parse "refs/tags/A^0") &&
	but describe --contains $tagged_cummit >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --contains and --match' '
	echo "A^0" >expect &&
	tagged_cummit=$(but rev-parse "refs/tags/A^0") &&
	test_must_fail but describe --contains --match="B" $tagged_cummit &&
	but describe --contains --match="B" --match="A" $tagged_cummit >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --exclude' '
	echo "c~1" >expect &&
	tagged_cummit=$(but rev-parse "refs/tags/A^0") &&
	test_must_fail but describe --contains --match="B" $tagged_cummit &&
	but describe --contains --match="?" --exclude="A" $tagged_cummit >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --contains and --no-match' '
	echo "A^0" >expect &&
	tagged_cummit=$(but rev-parse "refs/tags/A^0") &&
	but describe --contains --match="B" --no-match $tagged_cummit >actual &&
	test_cmp expect actual
'

test_expect_success 'setup and absorb a submodule' '
	test_create_repo sub1 &&
	test_cummit -C sub1 initial &&
	but submodule add ./sub1 &&
	but submodule absorbbutdirs &&
	but cummit -a -m "add submodule" &&
	but describe --dirty >expect &&
	but describe --broken >out &&
	test_cmp expect out
'

test_expect_success 'describe chokes on severely broken submodules' '
	mv .but/modules/sub1/ .but/modules/sub_moved &&
	test_must_fail but describe --dirty
'

test_expect_success 'describe ignoring a broken submodule' '
	but describe --broken >out &&
	grep broken out
'

test_expect_success 'describe with --work-tree ignoring a broken submodule' '
	(
		cd "$TEST_DIRECTORY" &&
		but --but-dir "$TRASH_DIRECTORY/.but" --work-tree "$TRASH_DIRECTORY" describe --broken >"$TRASH_DIRECTORY/out"
	) &&
	test_when_finished "mv .but/modules/sub_moved .but/modules/sub1" &&
	grep broken out
'

test_expect_success 'describe a blob at a directly tagged cummit' '
	echo "make it a unique blob" >file &&
	but add file && but cummit -m "content in file" &&
	but tag -a -m "latest annotated tag" unique-file &&
	but describe HEAD:file >actual &&
	echo "unique-file:file" >expect &&
	test_cmp expect actual
'

test_expect_success 'describe a blob with its first introduction' '
	but cummit --allow-empty -m "empty cummit" &&
	but rm file &&
	but cummit -m "delete blob" &&
	but revert HEAD &&
	but cummit --allow-empty -m "empty cummit" &&
	but describe HEAD:file >actual &&
	echo "unique-file:file" >expect &&
	test_cmp expect actual
'

test_expect_success 'describe directly tagged blob' '
	but tag test-blob unique-file:file &&
	but describe test-blob >actual &&
	echo "unique-file:file" >expect &&
	# suboptimal: we rather want to see "test-blob"
	test_cmp expect actual
'

test_expect_success 'describe tag object' '
	but tag test-blob-1 -a -m msg unique-file:file &&
	test_must_fail but describe test-blob-1 2>actual &&
	test_i18ngrep "fatal: test-blob-1 is neither a cummit nor blob" actual
'

test_expect_success ULIMIT_STACK_SIZE 'name-rev works in a deep repo' '
	i=1 &&
	while test $i -lt 8000
	do
		echo "cummit refs/heads/main
cummitter A U Thor <author@example.com> $((1000000000 + $i * 100)) +0200
data <<EOF
cummit #$i
EOF" &&
		if test $i = 1
		then
			echo "from refs/heads/main^0"
		fi &&
		i=$(($i + 1)) || return 1
	done | but fast-import &&
	but checkout main &&
	but tag far-far-away HEAD^ &&
	echo "HEAD~4000 tags/far-far-away~3999" >expect &&
	but name-rev HEAD~4000 >actual &&
	test_cmp expect actual &&
	run_with_limited_stack but name-rev HEAD~4000 >actual &&
	test_cmp expect actual
'

test_expect_success ULIMIT_STACK_SIZE 'describe works in a deep repo' '
	but tag -f far-far-away HEAD~7999 &&
	echo "far-far-away" >expect &&
	but describe --tags --abbrev=0 HEAD~4000 >actual &&
	test_cmp expect actual &&
	run_with_limited_stack but describe --tags --abbrev=0 HEAD~4000 >actual &&
	test_cmp expect actual
'

check_describe tags/A --all A
check_describe tags/c --all c
check_describe heads/branch_A --all --match='branch_*' branch_A

test_expect_success 'describe complains about tree object' '
	test_must_fail but describe HEAD^{tree}
'

test_expect_success 'describe complains about missing object' '
	test_must_fail but describe $ZERO_OID
'

test_expect_success 'name-rev a rev shortly after epoch' '
	test_when_finished "but checkout main" &&

	but checkout --orphan no-timestamp-underflow &&
	# Any date closer to epoch than the CUTOFF_DATE_SLOP constant
	# in builtin/name-rev.c.
	BUT_CUMMITTER_DATE="@1234 +0000" \
	but cummit -m "cummitter date shortly after epoch" &&
	old_cummit_oid=$(but rev-parse HEAD) &&

	echo "$old_cummit_oid no-timestamp-underflow" >expect &&
	but name-rev $old_cummit_oid >actual &&
	test_cmp expect actual
'

# A--------------main
#  \            /
#   \----------M2
#    \        /
#     \---M1-C
#      \ /
#       B
test_expect_success 'name-rev covers all conditions while looking at parents' '
	but init repo &&
	(
		cd repo &&

		echo A >file &&
		but add file &&
		but cummit -m A &&
		A=$(but rev-parse HEAD) &&

		but checkout --detach &&
		echo B >file &&
		but cummit -m B file &&
		B=$(but rev-parse HEAD) &&

		but checkout $A &&
		but merge --no-ff $B &&  # M1

		echo C >file &&
		but cummit -m C file &&

		but checkout $A &&
		but merge --no-ff HEAD@{1} && # M2

		but checkout main &&
		but merge --no-ff HEAD@{1} &&

		echo "$B main^2^2~1^2" >expect &&
		but name-rev $B >actual &&

		test_cmp expect actual
	)
'

# A-B-C-D-E-main
#
# Where C has a non-monotonically increasing cummit timestamp w.r.t. other
# cummits
test_expect_success 'non-monotonic cummit dates setup' '
	UNIX_EPOCH_ZERO="@0 +0000" &&
	but init non-monotonic &&
	test_cummit -C non-monotonic A &&
	test_cummit -C non-monotonic --no-tag B &&
	test_cummit -C non-monotonic --no-tag --date "$UNIX_EPOCH_ZERO" C &&
	test_cummit -C non-monotonic D &&
	test_cummit -C non-monotonic E
'

test_expect_success 'name-rev with cummitGraph handles non-monotonic timestamps' '
	test_config -C non-monotonic core.cummitGraph true &&
	(
		cd non-monotonic &&

		but cummit-graph write --reachable &&

		echo "main~3 tags/D~2" >expect &&
		but name-rev --tags main~3 >actual &&

		test_cmp expect actual
	)
'

test_expect_success 'name-rev --all works with non-monotonic timestamps' '
	test_config -C non-monotonic core.cummitGraph false &&
	(
		cd non-monotonic &&

		rm -rf .but/info/cummit-graph* &&

		cat >tags <<-\EOF &&
		tags/E
		tags/D
		tags/D~1
		tags/D~2
		tags/A
		EOF

		but log --pretty=%H >revs &&

		paste -d" " revs tags | sort >expect &&

		but name-rev --tags --all | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'name-rev --annotate-stdin works with non-monotonic timestamps' '
	test_config -C non-monotonic core.cummitGraph false &&
	(
		cd non-monotonic &&

		rm -rf .but/info/cummit-graph* &&

		cat >expect <<-\EOF &&
		E
		D
		D~1
		D~2
		A
		EOF

		but log --pretty=%H >revs &&
		but name-rev --tags --annotate-stdin --name-only <revs >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'name-rev --all works with cummitGraph' '
	test_config -C non-monotonic core.cummitGraph true &&
	(
		cd non-monotonic &&

		but cummit-graph write --reachable &&

		cat >tags <<-\EOF &&
		tags/E
		tags/D
		tags/D~1
		tags/D~2
		tags/A
		EOF

		but log --pretty=%H >revs &&

		paste -d" " revs tags | sort >expect &&

		but name-rev --tags --all | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'name-rev --annotate-stdin works with cummitGraph' '
	test_config -C non-monotonic core.cummitGraph true &&
	(
		cd non-monotonic &&

		but cummit-graph write --reachable &&

		cat >expect <<-\EOF &&
		E
		D
		D~1
		D~2
		A
		EOF

		but log --pretty=%H >revs &&
		but name-rev --tags --annotate-stdin --name-only <revs >actual &&
		test_cmp expect actual
	)
'

#               B
#               o
#                \
#  o-----o---o----x
#        A
#
test_expect_success 'setup: describe cummits with disjoint bases' '
	but init disjoint1 &&
	(
		cd disjoint1 &&

		echo o >> file && but add file && but cummit -m o &&
		echo A >> file && but add file && but cummit -m A &&
		but tag A -a -m A &&
		echo o >> file && but add file && but cummit -m o &&

		but checkout --orphan branch && rm file &&
		echo B > file2 && but add file2 && but cummit -m B &&
		but tag B -a -m B &&
		but merge --no-ff --allow-unrelated-histories main -m x
	)
'

check_describe -C disjoint1 "A-3-gHASH" HEAD

#           B
#   o---o---o------------.
#                         \
#                  o---o---x
#                  A
#
test_expect_success 'setup: describe cummits with disjoint bases 2' '
	but init disjoint2 &&
	(
		cd disjoint2 &&

		echo A >> file && but add file && BUT_CUMMITTER_DATE="2020-01-01 18:00" but cummit -m A &&
		but tag A -a -m A &&
		echo o >> file && but add file && BUT_CUMMITTER_DATE="2020-01-01 18:01" but cummit -m o &&

		but checkout --orphan branch &&
		echo o >> file2 && but add file2 && BUT_CUMMITTER_DATE="2020-01-01 15:00" but cummit -m o &&
		echo o >> file2 && but add file2 && BUT_CUMMITTER_DATE="2020-01-01 15:01" but cummit -m o &&
		echo B >> file2 && but add file2 && BUT_CUMMITTER_DATE="2020-01-01 15:02" but cummit -m B &&
		but tag B -a -m B &&
		but merge --no-ff --allow-unrelated-histories main -m x
	)
'

check_describe -C disjoint2 "B-3-gHASH" HEAD

test_done
