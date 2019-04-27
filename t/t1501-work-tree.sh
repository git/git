#!/bin/sh

test_description='test separate work tree'
. ./test-lib.sh

test_expect_success 'setup' '
	EMPTY_TREE=$(git write-tree) &&
	EMPTY_BLOB=$(git hash-object -t blob --stdin </dev/null) &&
	CHANGED_BLOB=$(echo changed | git hash-object -t blob --stdin) &&
	EMPTY_BLOB7=$(echo $EMPTY_BLOB | sed "s/\(.......\).*/\1/") &&
	CHANGED_BLOB7=$(echo $CHANGED_BLOB | sed "s/\(.......\).*/\1/") &&

	mkdir -p work/sub/dir &&
	mkdir -p work2 &&
	mv .git repo.git
'

test_expect_success 'setup: helper for testing rev-parse' '
	test_rev_parse() {
		echo $1 >expected.bare &&
		echo $2 >expected.inside-git &&
		echo $3 >expected.inside-worktree &&
		if test $# -ge 4
		then
			echo $4 >expected.prefix
		fi &&

		git rev-parse --is-bare-repository >actual.bare &&
		git rev-parse --is-inside-git-dir >actual.inside-git &&
		git rev-parse --is-inside-work-tree >actual.inside-worktree &&
		if test $# -ge 4
		then
			git rev-parse --show-prefix >actual.prefix
		fi &&

		test_cmp expected.bare actual.bare &&
		test_cmp expected.inside-git actual.inside-git &&
		test_cmp expected.inside-worktree actual.inside-worktree &&
		if test $# -ge 4
		then
			# rev-parse --show-prefix should output
			# a single newline when at the top of the work tree,
			# but we test for that separately.
			test -z "$4" && test_must_be_empty actual.prefix ||
			test_cmp expected.prefix actual.prefix
		fi
	}
'

test_expect_success 'setup: core.worktree = relative path' '
	sane_unset GIT_WORK_TREE &&
	GIT_DIR=repo.git &&
	GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
	export GIT_DIR GIT_CONFIG &&
	git config core.worktree ../work
'

test_expect_success 'outside' '
	test_rev_parse false false false
'

test_expect_success 'inside work tree' '
	(
		cd work &&
		GIT_DIR=../repo.git &&
		GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
		test_rev_parse false false true ""
	)
'

test_expect_success 'empty prefix is actually written out' '
	echo >expected &&
	(
		cd work &&
		GIT_DIR=../repo.git &&
		GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
		git rev-parse --show-prefix >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'subdir of work tree' '
	(
		cd work/sub/dir &&
		GIT_DIR=../../../repo.git &&
		GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
		test_rev_parse false false true sub/dir/
	)
'

test_expect_success 'setup: core.worktree = absolute path' '
	sane_unset GIT_WORK_TREE &&
	GIT_DIR=$(pwd)/repo.git &&
	GIT_CONFIG=$GIT_DIR/config &&
	export GIT_DIR GIT_CONFIG &&
	git config core.worktree "$(pwd)/work"
'

test_expect_success 'outside' '
	test_rev_parse false false false &&
	(
		cd work2 &&
		test_rev_parse false false false
	)
'

test_expect_success 'inside work tree' '
	(
		cd work &&
		test_rev_parse false false true ""
	)
'

test_expect_success 'subdir of work tree' '
	(
		cd work/sub/dir &&
		test_rev_parse false false true sub/dir/
	)
'

test_expect_success 'setup: GIT_WORK_TREE=relative (override core.worktree)' '
	GIT_DIR=$(pwd)/repo.git &&
	GIT_CONFIG=$GIT_DIR/config &&
	git config core.worktree non-existent &&
	GIT_WORK_TREE=work &&
	export GIT_DIR GIT_CONFIG GIT_WORK_TREE
'

test_expect_success 'outside' '
	test_rev_parse false false false &&
	(
		cd work2 &&
		test_rev_parse false false false
	)
'

test_expect_success 'inside work tree' '
	(
		cd work &&
		GIT_WORK_TREE=. &&
		test_rev_parse false false true ""
	)
'

test_expect_success 'subdir of work tree' '
	(
		cd work/sub/dir &&
		GIT_WORK_TREE=../.. &&
		test_rev_parse false false true sub/dir/
	)
'

test_expect_success 'setup: GIT_WORK_TREE=absolute, below git dir' '
	mv work repo.git/work &&
	mv work2 repo.git/work2 &&
	GIT_DIR=$(pwd)/repo.git &&
	GIT_CONFIG=$GIT_DIR/config &&
	GIT_WORK_TREE=$(pwd)/repo.git/work &&
	export GIT_DIR GIT_CONFIG GIT_WORK_TREE
'

test_expect_success 'outside' '
	echo outside &&
	test_rev_parse false false false
'

test_expect_success 'in repo.git' '
	(
		cd repo.git &&
		test_rev_parse false true false
	) &&
	(
		cd repo.git/objects &&
		test_rev_parse false true false
	) &&
	(
		cd repo.git/work2 &&
		test_rev_parse false true false
	)
'

test_expect_success 'inside work tree' '
	(
		cd repo.git/work &&
		test_rev_parse false true true ""
	)
'

test_expect_success 'subdir of work tree' '
	(
		cd repo.git/work/sub/dir &&
		test_rev_parse false true true sub/dir/
	)
'

test_expect_success 'find work tree from repo' '
	echo sub/dir/untracked >expected &&
	cat <<-\EOF >repo.git/work/.gitignore &&
	expected.*
	actual.*
	.gitignore
	EOF
	>repo.git/work/sub/dir/untracked &&
	(
		cd repo.git &&
		git ls-files --others --exclude-standard >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'find work tree from work tree' '
	echo sub/dir/tracked >expected &&
	>repo.git/work/sub/dir/tracked &&
	(
		cd repo.git/work/sub/dir &&
		git --git-dir=../../.. add tracked
	) &&
	(
		cd repo.git &&
		git ls-files >../actual
	) &&
	test_cmp expected actual
'

test_expect_success '_gently() groks relative GIT_DIR & GIT_WORK_TREE' '
	(
		cd repo.git/work/sub/dir &&
		GIT_DIR=../../.. &&
		GIT_WORK_TREE=../.. &&
		GIT_PAGER= &&
		export GIT_DIR GIT_WORK_TREE GIT_PAGER &&

		git diff --exit-code tracked &&
		echo changed >tracked &&
		test_must_fail git diff --exit-code tracked
	)
'

test_expect_success 'diff-index respects work tree under .git dir' '
	cat >diff-index-cached.expected <<-EOF &&
	:000000 100644 $ZERO_OID $EMPTY_BLOB A	sub/dir/tracked
	EOF
	cat >diff-index.expected <<-EOF &&
	:000000 100644 $ZERO_OID $ZERO_OID A	sub/dir/tracked
	EOF

	(
		GIT_DIR=repo.git &&
		GIT_WORK_TREE=repo.git/work &&
		export GIT_DIR GIT_WORK_TREE &&
		git diff-index $EMPTY_TREE >diff-index.actual &&
		git diff-index --cached $EMPTY_TREE >diff-index-cached.actual
	) &&
	test_cmp diff-index.expected diff-index.actual &&
	test_cmp diff-index-cached.expected diff-index-cached.actual
'

test_expect_success 'diff-files respects work tree under .git dir' '
	cat >diff-files.expected <<-EOF &&
	:100644 100644 $EMPTY_BLOB $ZERO_OID M	sub/dir/tracked
	EOF

	(
		GIT_DIR=repo.git &&
		GIT_WORK_TREE=repo.git/work &&
		export GIT_DIR GIT_WORK_TREE &&
		git diff-files >diff-files.actual
	) &&
	test_cmp diff-files.expected diff-files.actual
'

test_expect_success 'git diff respects work tree under .git dir' '
	cat >diff-TREE.expected <<-EOF &&
	diff --git a/sub/dir/tracked b/sub/dir/tracked
	new file mode 100644
	index 0000000..$CHANGED_BLOB7
	--- /dev/null
	+++ b/sub/dir/tracked
	@@ -0,0 +1 @@
	+changed
	EOF
	cat >diff-TREE-cached.expected <<-EOF &&
	diff --git a/sub/dir/tracked b/sub/dir/tracked
	new file mode 100644
	index 0000000..$EMPTY_BLOB7
	EOF
	cat >diff-FILES.expected <<-EOF &&
	diff --git a/sub/dir/tracked b/sub/dir/tracked
	index $EMPTY_BLOB7..$CHANGED_BLOB7 100644
	--- a/sub/dir/tracked
	+++ b/sub/dir/tracked
	@@ -0,0 +1 @@
	+changed
	EOF

	(
		GIT_DIR=repo.git &&
		GIT_WORK_TREE=repo.git/work &&
		export GIT_DIR GIT_WORK_TREE &&
		git diff $EMPTY_TREE >diff-TREE.actual &&
		git diff --cached $EMPTY_TREE >diff-TREE-cached.actual &&
		git diff >diff-FILES.actual
	) &&
	test_cmp diff-TREE.expected diff-TREE.actual &&
	test_cmp diff-TREE-cached.expected diff-TREE-cached.actual &&
	test_cmp diff-FILES.expected diff-FILES.actual
'

test_expect_success 'git grep' '
	echo dir/tracked >expected.grep &&
	(
		cd repo.git/work/sub &&
		GIT_DIR=../.. &&
		GIT_WORK_TREE=.. &&
		export GIT_DIR GIT_WORK_TREE &&
		git grep -l changed >../../../actual.grep
	) &&
	test_cmp expected.grep actual.grep
'

test_expect_success 'git commit' '
	(
		cd repo.git &&
		GIT_DIR=. GIT_WORK_TREE=work git commit -a -m done
	)
'

test_expect_success 'absolute pathspec should fail gracefully' '
	(
		cd repo.git &&
		test_might_fail git config --unset core.worktree &&
		test_must_fail git log HEAD -- /home
	)
'

test_expect_success 'make_relative_path handles double slashes in GIT_DIR' '
	>dummy_file &&
	echo git --git-dir="$(pwd)//repo.git" --work-tree="$(pwd)" add dummy_file &&
	git --git-dir="$(pwd)//repo.git" --work-tree="$(pwd)" add dummy_file
'

test_expect_success 'relative $GIT_WORK_TREE and git subprocesses' '
	GIT_DIR=repo.git GIT_WORK_TREE=repo.git/work \
	test-tool subprocess --setup-work-tree rev-parse --show-toplevel >actual &&
	echo "$(pwd)/repo.git/work" >expected &&
	test_cmp expected actual
'

test_expect_success 'Multi-worktree setup' '
	mkdir work &&
	mkdir -p repo.git/repos/foo &&
	cp repo.git/HEAD repo.git/index repo.git/repos/foo &&
	test_might_fail cp repo.git/sharedindex.* repo.git/repos/foo &&
	sane_unset GIT_DIR GIT_CONFIG GIT_WORK_TREE
'

test_expect_success 'GIT_DIR set (1)' '
	echo "gitdir: repo.git/repos/foo" >gitfile &&
	echo ../.. >repo.git/repos/foo/commondir &&
	(
		cd work &&
		GIT_DIR=../gitfile git rev-parse --git-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.git" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'GIT_DIR set (2)' '
	echo "gitdir: repo.git/repos/foo" >gitfile &&
	echo "$(pwd)/repo.git" >repo.git/repos/foo/commondir &&
	(
		cd work &&
		GIT_DIR=../gitfile git rev-parse --git-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.git" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'Auto discovery' '
	echo "gitdir: repo.git/repos/foo" >.git &&
	echo ../.. >repo.git/repos/foo/commondir &&
	(
		cd work &&
		git rev-parse --git-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.git" >expect &&
		test_cmp expect actual &&
		echo haha >data1 &&
		git add data1 &&
		git ls-files --full-name :/ | grep data1 >actual &&
		echo work/data1 >expect &&
		test_cmp expect actual
	)
'

test_expect_success '$GIT_DIR/common overrides core.worktree' '
	mkdir elsewhere &&
	git --git-dir=repo.git config core.worktree "$TRASH_DIRECTORY/elsewhere" &&
	echo "gitdir: repo.git/repos/foo" >.git &&
	echo ../.. >repo.git/repos/foo/commondir &&
	(
		cd work &&
		git rev-parse --git-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.git" >expect &&
		test_cmp expect actual &&
		echo haha >data2 &&
		git add data2 &&
		git ls-files --full-name :/ | grep data2 >actual &&
		echo work/data2 >expect &&
		test_cmp expect actual
	)
'

test_expect_success '$GIT_WORK_TREE overrides $GIT_DIR/common' '
	echo "gitdir: repo.git/repos/foo" >.git &&
	echo ../.. >repo.git/repos/foo/commondir &&
	(
		cd work &&
		echo haha >data3 &&
		git --git-dir=../.git --work-tree=. add data3 &&
		git ls-files --full-name -- :/ | grep data3 >actual &&
		echo data3 >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'error out gracefully on invalid $GIT_WORK_TREE' '
	(
		GIT_WORK_TREE=/.invalid/work/tree &&
		export GIT_WORK_TREE &&
		test_expect_code 128 git rev-parse
	)
'

test_expect_success 'refs work with relative gitdir and work tree' '
	git init relative &&
	git -C relative commit --allow-empty -m one &&
	git -C relative commit --allow-empty -m two &&

	GIT_DIR=relative/.git GIT_WORK_TREE=relative git reset HEAD^ &&

	git -C relative log -1 --format=%s >actual &&
	echo one >expect &&
	test_cmp expect actual
'

test_done
