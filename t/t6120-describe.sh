#!/bin/sh

test_description='test describe'

#  o---o-----o----o----o-------o----x
#       \   D,R   e           /
#        \---o-------------o-'
#         \  B            /
#          `-o----o----o-'
#                 A    c
#
# First parent of a merge commit is on the same line, second parent below.

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
		git ${indir:+ -C "$indir"} describe $describe_opts >raw &&
		sed -e "s/-g[0-9a-f]*\$/-gHASH/" <raw >actual &&
		echo "$expect" >expect &&
		test_cmp expect actual
	'
}

test_expect_success setup '
	test_commit initial file one &&
	test_commit second file two &&
	test_commit third file three &&
	test_commit --annotate A file A &&
	test_commit c file c &&

	git reset --hard second &&
	test_commit --annotate B side B &&

	test_tick &&
	git merge -m Merged c &&
	merged=$(git rev-parse HEAD) &&

	git reset --hard second &&
	test_commit --no-tag D another D &&

	test_tick &&
	git tag -a -m R R &&

	test_commit e another DD &&
	test_commit --no-tag "yet another" another DDD &&

	test_tick &&
	git merge -m Merged $merged &&

	test_commit --no-tag x file
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

test_expect_success 'describe --contains defaults to HEAD without commit-ish' '
	echo "A^0" >expect &&
	git checkout A &&
	test_when_finished "git checkout -" &&
	git describe --contains >actual &&
	test_cmp expect actual
'

check_describe tags/A --all A^0

test_expect_success 'renaming tag A to Q locally produces a warning' "
	mv .git/refs/tags/A .git/refs/tags/Q &&
	git describe HEAD 2>err >out &&
	cat >expected <<-\EOF &&
	warning: tag 'Q' is externally known as 'A'
	EOF
	test_cmp expected err &&
	grep -E '^A-8-g[0-9a-f]+$' out
"

test_expect_success 'misnamed annotated tag forces long output' '
	description=$(git describe --no-long Q^0) &&
	expr "$description" : "A-0-g[0-9a-f]*$" &&
	git rev-parse --verify "$description" >actual &&
	git rev-parse --verify Q^0 >expect &&
	test_cmp expect actual
'

test_expect_success 'abbrev=0 will not break misplaced tag (1)' '
	description=$(git describe --abbrev=0 Q^0) &&
	expr "$description" : "A-0-g[0-9a-f]*$"
'

test_expect_success 'abbrev=0 will not break misplaced tag (2)' '
	description=$(git describe --abbrev=0 c^0) &&
	expr "$description" : "A-1-g[0-9a-f]*$"
'

test_expect_success 'rename tag Q back to A' '
	mv .git/refs/tags/Q .git/refs/tags/A
'

test_expect_success 'pack tag refs' 'git pack-refs'
check_describe A-8-gHASH HEAD

test_expect_success 'describe works from outside repo using --git-dir' '
	git clone --bare "$TRASH_DIRECTORY" "$TRASH_DIRECTORY/bare" &&
	git --git-dir "$TRASH_DIRECTORY/bare" describe >out &&
	grep -E "^A-8-g[0-9a-f]+$" out
'

check_describe "A-8-gHASH" --dirty

test_expect_success 'describe --dirty with --work-tree' '
	(
		cd "$TEST_DIRECTORY" &&
		git --git-dir "$TRASH_DIRECTORY/.git" --work-tree "$TRASH_DIRECTORY" describe --dirty >"$TRASH_DIRECTORY/out"
	) &&
	grep -E "^A-8-g[0-9a-f]+$" out
'

test_expect_success 'set-up dirty work tree' '
	echo >>file
'

test_expect_success 'describe --dirty with --work-tree (dirty)' '
	git describe --dirty >expected &&
	(
		cd "$TEST_DIRECTORY" &&
		git --git-dir "$TRASH_DIRECTORY/.git" --work-tree "$TRASH_DIRECTORY" describe --dirty >"$TRASH_DIRECTORY/out"
	) &&
	grep -E "^A-8-g[0-9a-f]+-dirty$" out &&
	test_cmp expected out
'

test_expect_success 'describe --dirty=.mod with --work-tree (dirty)' '
	git describe --dirty=.mod >expected &&
	(
		cd "$TEST_DIRECTORY" &&
		git --git-dir "$TRASH_DIRECTORY/.git" --work-tree "$TRASH_DIRECTORY" describe --dirty=.mod >"$TRASH_DIRECTORY/out"
	) &&
	grep -E "^A-8-g[0-9a-f]+.mod$" out &&
	test_cmp expected out
'

test_expect_success 'describe --dirty HEAD' '
	test_must_fail git describe --dirty HEAD
'

test_expect_success 'set-up matching pattern tests' '
	git tag -a -m test-annotated test-annotated &&
	echo >>file &&
	test_tick &&
	git commit -a -m "one more" &&
	git tag test1-lightweight &&
	echo >>file &&
	test_tick &&
	git commit -a -m "yet another" &&
	git tag test2-lightweight &&
	echo >>file &&
	test_tick &&
	git commit -a -m "even more"

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
	git branch branch_A A &&
	git branch branch_C c &&
	git update-ref refs/remotes/origin/remote_branch_A "A^{commit}" &&
	git update-ref refs/remotes/origin/remote_branch_C "c^{commit}" &&
	git update-ref refs/original/original_branch_A test-annotated~2
'

check_describe "heads/branch_A-11-gHASH" --all --match="branch_*" --exclude="branch_C" HEAD

check_describe "remotes/origin/remote_branch_A-11-gHASH" --all --match="origin/remote_branch_*" --exclude="origin/remote_branch_C" HEAD

check_describe "original/original_branch_A-6-gHASH" --all test-annotated~1

test_expect_success '--match does not work for other types' '
	test_must_fail git describe --all --match="*original_branch_*" test-annotated~1
'

test_expect_success '--exclude does not work for other types' '
	R=$(git describe --all --exclude="any_pattern_even_not_matching" test-annotated~1) &&
	case "$R" in
	*original_branch_A*) echo "fail: Found unknown reference $R with --exclude"
		false;;
	*) echo ok: Found some known type;;
	esac
'

test_expect_success 'name-rev with exact tags' '
	echo A >expect &&
	tag_object=$(git rev-parse refs/tags/A) &&
	git name-rev --tags --name-only $tag_object >actual &&
	test_cmp expect actual &&

	echo "A^0" >expect &&
	tagged_commit=$(git rev-parse "refs/tags/A^0") &&
	git name-rev --tags --name-only $tagged_commit >actual &&
	test_cmp expect actual
'

test_expect_success 'name-rev --all' '
	>expect.unsorted &&
	for rev in $(git rev-list --all)
	do
		git name-rev $rev >>expect.unsorted
	done &&
	sort <expect.unsorted >expect &&
	git name-rev --all >actual.unsorted &&
	sort <actual.unsorted >actual &&
	test_cmp expect actual
'

test_expect_success 'name-rev --stdin' '
	>expect.unsorted &&
	for rev in $(git rev-list --all)
	do
		name=$(git name-rev --name-only $rev) &&
		echo "$rev ($name)" >>expect.unsorted
	done &&
	sort <expect.unsorted >expect &&
	git rev-list --all | git name-rev --stdin >actual.unsorted &&
	sort <actual.unsorted >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --contains with the exact tags' '
	echo "A^0" >expect &&
	tag_object=$(git rev-parse refs/tags/A) &&
	git describe --contains $tag_object >actual &&
	test_cmp expect actual &&

	echo "A^0" >expect &&
	tagged_commit=$(git rev-parse "refs/tags/A^0") &&
	git describe --contains $tagged_commit >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --contains and --match' '
	echo "A^0" >expect &&
	tagged_commit=$(git rev-parse "refs/tags/A^0") &&
	test_must_fail git describe --contains --match="B" $tagged_commit &&
	git describe --contains --match="B" --match="A" $tagged_commit >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --exclude' '
	echo "c~1" >expect &&
	tagged_commit=$(git rev-parse "refs/tags/A^0") &&
	test_must_fail git describe --contains --match="B" $tagged_commit &&
	git describe --contains --match="?" --exclude="A" $tagged_commit >actual &&
	test_cmp expect actual
'

test_expect_success 'describe --contains and --no-match' '
	echo "A^0" >expect &&
	tagged_commit=$(git rev-parse "refs/tags/A^0") &&
	git describe --contains --match="B" --no-match $tagged_commit >actual &&
	test_cmp expect actual
'

test_expect_success 'setup and absorb a submodule' '
	test_create_repo sub1 &&
	test_commit -C sub1 initial &&
	git submodule add ./sub1 &&
	git submodule absorbgitdirs &&
	git commit -a -m "add submodule" &&
	git describe --dirty >expect &&
	git describe --broken >out &&
	test_cmp expect out
'

test_expect_success 'describe chokes on severely broken submodules' '
	mv .git/modules/sub1/ .git/modules/sub_moved &&
	test_must_fail git describe --dirty
'

test_expect_success 'describe ignoring a broken submodule' '
	git describe --broken >out &&
	grep broken out
'

test_expect_success 'describe with --work-tree ignoring a broken submodule' '
	(
		cd "$TEST_DIRECTORY" &&
		git --git-dir "$TRASH_DIRECTORY/.git" --work-tree "$TRASH_DIRECTORY" describe --broken >"$TRASH_DIRECTORY/out"
	) &&
	test_when_finished "mv .git/modules/sub_moved .git/modules/sub1" &&
	grep broken out
'

test_expect_success 'describe a blob at a directly tagged commit' '
	echo "make it a unique blob" >file &&
	git add file && git commit -m "content in file" &&
	git tag -a -m "latest annotated tag" unique-file &&
	git describe HEAD:file >actual &&
	echo "unique-file:file" >expect &&
	test_cmp expect actual
'

test_expect_success 'describe a blob with its first introduction' '
	git commit --allow-empty -m "empty commit" &&
	git rm file &&
	git commit -m "delete blob" &&
	git revert HEAD &&
	git commit --allow-empty -m "empty commit" &&
	git describe HEAD:file >actual &&
	echo "unique-file:file" >expect &&
	test_cmp expect actual
'

test_expect_success 'describe directly tagged blob' '
	git tag test-blob unique-file:file &&
	git describe test-blob >actual &&
	echo "unique-file:file" >expect &&
	# suboptimal: we rather want to see "test-blob"
	test_cmp expect actual
'

test_expect_success 'describe tag object' '
	git tag test-blob-1 -a -m msg unique-file:file &&
	test_must_fail git describe test-blob-1 2>actual &&
	test_i18ngrep "fatal: test-blob-1 is neither a commit nor blob" actual
'

test_expect_success ULIMIT_STACK_SIZE 'name-rev works in a deep repo' '
	i=1 &&
	while test $i -lt 8000
	do
		echo "commit refs/heads/main
committer A U Thor <author@example.com> $((1000000000 + $i * 100)) +0200
data <<EOF
commit #$i
EOF"
		test $i = 1 && echo "from refs/heads/main^0"
		i=$(($i + 1))
	done | git fast-import &&
	git checkout main &&
	git tag far-far-away HEAD^ &&
	echo "HEAD~4000 tags/far-far-away~3999" >expect &&
	git name-rev HEAD~4000 >actual &&
	test_cmp expect actual &&
	run_with_limited_stack git name-rev HEAD~4000 >actual &&
	test_cmp expect actual
'

test_expect_success ULIMIT_STACK_SIZE 'describe works in a deep repo' '
	git tag -f far-far-away HEAD~7999 &&
	echo "far-far-away" >expect &&
	git describe --tags --abbrev=0 HEAD~4000 >actual &&
	test_cmp expect actual &&
	run_with_limited_stack git describe --tags --abbrev=0 HEAD~4000 >actual &&
	test_cmp expect actual
'

check_describe tags/A --all A
check_describe tags/c --all c
check_describe heads/branch_A --all --match='branch_*' branch_A

test_expect_success 'describe complains about tree object' '
	test_must_fail git describe HEAD^{tree}
'

test_expect_success 'describe complains about missing object' '
	test_must_fail git describe $ZERO_OID
'

test_expect_success 'name-rev a rev shortly after epoch' '
	test_when_finished "git checkout main" &&

	git checkout --orphan no-timestamp-underflow &&
	# Any date closer to epoch than the CUTOFF_DATE_SLOP constant
	# in builtin/name-rev.c.
	GIT_COMMITTER_DATE="@1234 +0000" \
	git commit -m "committer date shortly after epoch" &&
	old_commit_oid=$(git rev-parse HEAD) &&

	echo "$old_commit_oid no-timestamp-underflow" >expect &&
	git name-rev $old_commit_oid >actual &&
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
	git init repo &&
	(
		cd repo &&

		echo A >file &&
		git add file &&
		git commit -m A &&
		A=$(git rev-parse HEAD) &&

		git checkout --detach &&
		echo B >file &&
		git commit -m B file &&
		B=$(git rev-parse HEAD) &&

		git checkout $A &&
		git merge --no-ff $B &&  # M1

		echo C >file &&
		git commit -m C file &&

		git checkout $A &&
		git merge --no-ff HEAD@{1} && # M2

		git checkout main &&
		git merge --no-ff HEAD@{1} &&

		echo "$B main^2^2~1^2" >expect &&
		git name-rev $B >actual &&

		test_cmp expect actual
	)
'

#               B
#               o
#                \
#  o-----o---o----x
#        A
#
test_expect_success 'setup: describe commits with disjoint bases' '
	git init disjoint1 &&
	(
		cd disjoint1 &&

		echo o >> file && git add file && git commit -m o &&
		echo A >> file && git add file && git commit -m A &&
		git tag A -a -m A &&
		echo o >> file && git add file && git commit -m o &&

		git checkout --orphan branch && rm file &&
		echo B > file2 && git add file2 && git commit -m B &&
		git tag B -a -m B &&
		git merge --no-ff --allow-unrelated-histories main -m x
	)
'

check_describe -C disjoint1 "A-3-gHASH" HEAD

#           B
#   o---o---o------------.
#                         \
#                  o---o---x
#                  A
#
test_expect_success 'setup: describe commits with disjoint bases 2' '
	git init disjoint2 &&
	(
		cd disjoint2 &&

		echo A >> file && git add file && GIT_COMMITTER_DATE="2020-01-01 18:00" git commit -m A &&
		git tag A -a -m A &&
		echo o >> file && git add file && GIT_COMMITTER_DATE="2020-01-01 18:01" git commit -m o &&

		git checkout --orphan branch &&
		echo o >> file2 && git add file2 && GIT_COMMITTER_DATE="2020-01-01 15:00" git commit -m o &&
		echo o >> file2 && git add file2 && GIT_COMMITTER_DATE="2020-01-01 15:01" git commit -m o &&
		echo B >> file2 && git add file2 && GIT_COMMITTER_DATE="2020-01-01 15:02" git commit -m B &&
		git tag B -a -m B &&
		git merge --no-ff --allow-unrelated-histories main -m x
	)
'

check_describe -C disjoint2 "B-3-gHASH" HEAD

test_done
