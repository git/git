#!/bin/sh

test_description='test separate work tree'
. ./test-lib.sh

test_expect_success 'setup' '
	EMPTY_TREE=$(but write-tree) &&
	EMPTY_BLOB=$(but hash-object -t blob --stdin </dev/null) &&
	CHANGED_BLOB=$(echo changed | but hash-object -t blob --stdin) &&
	EMPTY_BLOB7=$(echo $EMPTY_BLOB | sed "s/\(.......\).*/\1/") &&
	CHANGED_BLOB7=$(echo $CHANGED_BLOB | sed "s/\(.......\).*/\1/") &&

	mkdir -p work/sub/dir &&
	mkdir -p work2 &&
	mv .but repo.but
'

test_expect_success 'setup: helper for testing rev-parse' '
	test_rev_parse() {
		echo $1 >expected.bare &&
		echo $2 >expected.inside-but &&
		echo $3 >expected.inside-worktree &&
		if test $# -ge 4
		then
			echo $4 >expected.prefix
		fi &&

		but rev-parse --is-bare-repository >actual.bare &&
		but rev-parse --is-inside-but-dir >actual.inside-but &&
		but rev-parse --is-inside-work-tree >actual.inside-worktree &&
		if test $# -ge 4
		then
			but rev-parse --show-prefix >actual.prefix
		fi &&

		test_cmp expected.bare actual.bare &&
		test_cmp expected.inside-but actual.inside-but &&
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
	GIT_DIR=repo.but &&
	GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
	export GIT_DIR GIT_CONFIG &&
	but config core.worktree ../work
'

test_expect_success 'outside' '
	test_rev_parse false false false
'

test_expect_success 'inside work tree' '
	(
		cd work &&
		GIT_DIR=../repo.but &&
		GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
		test_rev_parse false false true ""
	)
'

test_expect_success 'empty prefix is actually written out' '
	echo >expected &&
	(
		cd work &&
		GIT_DIR=../repo.but &&
		GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
		but rev-parse --show-prefix >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'subdir of work tree' '
	(
		cd work/sub/dir &&
		GIT_DIR=../../../repo.but &&
		GIT_CONFIG="$(pwd)"/$GIT_DIR/config &&
		test_rev_parse false false true sub/dir/
	)
'

test_expect_success 'setup: core.worktree = absolute path' '
	sane_unset GIT_WORK_TREE &&
	GIT_DIR=$(pwd)/repo.but &&
	GIT_CONFIG=$GIT_DIR/config &&
	export GIT_DIR GIT_CONFIG &&
	but config core.worktree "$(pwd)/work"
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
	GIT_DIR=$(pwd)/repo.but &&
	GIT_CONFIG=$GIT_DIR/config &&
	but config core.worktree non-existent &&
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

test_expect_success 'setup: GIT_WORK_TREE=absolute, below but dir' '
	mv work repo.but/work &&
	mv work2 repo.but/work2 &&
	GIT_DIR=$(pwd)/repo.but &&
	GIT_CONFIG=$GIT_DIR/config &&
	GIT_WORK_TREE=$(pwd)/repo.but/work &&
	export GIT_DIR GIT_CONFIG GIT_WORK_TREE
'

test_expect_success 'outside' '
	echo outside &&
	test_rev_parse false false false
'

test_expect_success 'in repo.but' '
	(
		cd repo.but &&
		test_rev_parse false true false
	) &&
	(
		cd repo.but/objects &&
		test_rev_parse false true false
	) &&
	(
		cd repo.but/work2 &&
		test_rev_parse false true false
	)
'

test_expect_success 'inside work tree' '
	(
		cd repo.but/work &&
		test_rev_parse false true true ""
	)
'

test_expect_success 'subdir of work tree' '
	(
		cd repo.but/work/sub/dir &&
		test_rev_parse false true true sub/dir/
	)
'

test_expect_success 'find work tree from repo' '
	echo sub/dir/untracked >expected &&
	cat <<-\EOF >repo.but/work/.butignore &&
	expected.*
	actual.*
	.butignore
	EOF
	>repo.but/work/sub/dir/untracked &&
	(
		cd repo.but &&
		but ls-files --others --exclude-standard >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'find work tree from work tree' '
	echo sub/dir/tracked >expected &&
	>repo.but/work/sub/dir/tracked &&
	(
		cd repo.but/work/sub/dir &&
		but --but-dir=../../.. add tracked
	) &&
	(
		cd repo.but &&
		but ls-files >../actual
	) &&
	test_cmp expected actual
'

test_expect_success '_gently() groks relative GIT_DIR & GIT_WORK_TREE' '
	(
		cd repo.but/work/sub/dir &&
		GIT_DIR=../../.. &&
		GIT_WORK_TREE=../.. &&
		GIT_PAGER= &&
		export GIT_DIR GIT_WORK_TREE GIT_PAGER &&

		but diff --exit-code tracked &&
		echo changed >tracked &&
		test_must_fail but diff --exit-code tracked
	)
'

test_expect_success 'diff-index respects work tree under .but dir' '
	cat >diff-index-cached.expected <<-EOF &&
	:000000 100644 $ZERO_OID $EMPTY_BLOB A	sub/dir/tracked
	EOF
	cat >diff-index.expected <<-EOF &&
	:000000 100644 $ZERO_OID $ZERO_OID A	sub/dir/tracked
	EOF

	(
		GIT_DIR=repo.but &&
		GIT_WORK_TREE=repo.but/work &&
		export GIT_DIR GIT_WORK_TREE &&
		but diff-index $EMPTY_TREE >diff-index.actual &&
		but diff-index --cached $EMPTY_TREE >diff-index-cached.actual
	) &&
	test_cmp diff-index.expected diff-index.actual &&
	test_cmp diff-index-cached.expected diff-index-cached.actual
'

test_expect_success 'diff-files respects work tree under .but dir' '
	cat >diff-files.expected <<-EOF &&
	:100644 100644 $EMPTY_BLOB $ZERO_OID M	sub/dir/tracked
	EOF

	(
		GIT_DIR=repo.but &&
		GIT_WORK_TREE=repo.but/work &&
		export GIT_DIR GIT_WORK_TREE &&
		but diff-files >diff-files.actual
	) &&
	test_cmp diff-files.expected diff-files.actual
'

test_expect_success 'but diff respects work tree under .but dir' '
	cat >diff-TREE.expected <<-EOF &&
	diff --but a/sub/dir/tracked b/sub/dir/tracked
	new file mode 100644
	index 0000000..$CHANGED_BLOB7
	--- /dev/null
	+++ b/sub/dir/tracked
	@@ -0,0 +1 @@
	+changed
	EOF
	cat >diff-TREE-cached.expected <<-EOF &&
	diff --but a/sub/dir/tracked b/sub/dir/tracked
	new file mode 100644
	index 0000000..$EMPTY_BLOB7
	EOF
	cat >diff-FILES.expected <<-EOF &&
	diff --but a/sub/dir/tracked b/sub/dir/tracked
	index $EMPTY_BLOB7..$CHANGED_BLOB7 100644
	--- a/sub/dir/tracked
	+++ b/sub/dir/tracked
	@@ -0,0 +1 @@
	+changed
	EOF

	(
		GIT_DIR=repo.but &&
		GIT_WORK_TREE=repo.but/work &&
		export GIT_DIR GIT_WORK_TREE &&
		but diff $EMPTY_TREE >diff-TREE.actual &&
		but diff --cached $EMPTY_TREE >diff-TREE-cached.actual &&
		but diff >diff-FILES.actual
	) &&
	test_cmp diff-TREE.expected diff-TREE.actual &&
	test_cmp diff-TREE-cached.expected diff-TREE-cached.actual &&
	test_cmp diff-FILES.expected diff-FILES.actual
'

test_expect_success 'but grep' '
	echo dir/tracked >expected.grep &&
	(
		cd repo.but/work/sub &&
		GIT_DIR=../.. &&
		GIT_WORK_TREE=.. &&
		export GIT_DIR GIT_WORK_TREE &&
		but grep -l changed >../../../actual.grep
	) &&
	test_cmp expected.grep actual.grep
'

test_expect_success 'but cummit' '
	(
		cd repo.but &&
		GIT_DIR=. GIT_WORK_TREE=work but cummit -a -m done
	)
'

test_expect_success 'absolute pathspec should fail gracefully' '
	(
		cd repo.but &&
		test_might_fail but config --unset core.worktree &&
		test_must_fail but log HEAD -- /home
	)
'

test_expect_success 'make_relative_path handles double slashes in GIT_DIR' '
	>dummy_file &&
	echo but --but-dir="$(pwd)//repo.but" --work-tree="$(pwd)" add dummy_file &&
	but --but-dir="$(pwd)//repo.but" --work-tree="$(pwd)" add dummy_file
'

test_expect_success 'relative $GIT_WORK_TREE and but subprocesses' '
	GIT_DIR=repo.but GIT_WORK_TREE=repo.but/work \
	test-tool subprocess --setup-work-tree rev-parse --show-toplevel >actual &&
	echo "$(pwd)/repo.but/work" >expected &&
	test_cmp expected actual
'

test_expect_success 'Multi-worktree setup' '
	mkdir work &&
	mkdir -p repo.but/repos/foo &&
	cp repo.but/HEAD repo.but/index repo.but/repos/foo &&
	{ cp repo.but/sharedindex.* repo.but/repos/foo || :; } &&
	sane_unset GIT_DIR GIT_CONFIG GIT_WORK_TREE
'

test_expect_success 'GIT_DIR set (1)' '
	echo "butdir: repo.but/repos/foo" >butfile &&
	echo ../.. >repo.but/repos/foo/commondir &&
	(
		cd work &&
		GIT_DIR=../butfile but rev-parse --but-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.but" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'GIT_DIR set (2)' '
	echo "butdir: repo.but/repos/foo" >butfile &&
	echo "$(pwd)/repo.but" >repo.but/repos/foo/commondir &&
	(
		cd work &&
		GIT_DIR=../butfile but rev-parse --but-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.but" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'Auto discovery' '
	echo "butdir: repo.but/repos/foo" >.but &&
	echo ../.. >repo.but/repos/foo/commondir &&
	(
		cd work &&
		but rev-parse --but-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.but" >expect &&
		test_cmp expect actual &&
		echo haha >data1 &&
		but add data1 &&
		but ls-files --full-name :/ | grep data1 >actual &&
		echo work/data1 >expect &&
		test_cmp expect actual
	)
'

test_expect_success '$GIT_DIR/common overrides core.worktree' '
	mkdir elsewhere &&
	but --but-dir=repo.but config core.worktree "$TRASH_DIRECTORY/elsewhere" &&
	echo "butdir: repo.but/repos/foo" >.but &&
	echo ../.. >repo.but/repos/foo/commondir &&
	(
		cd work &&
		but rev-parse --but-common-dir >actual &&
		test-tool path-utils real_path "$TRASH_DIRECTORY/repo.but" >expect &&
		test_cmp expect actual &&
		echo haha >data2 &&
		but add data2 &&
		but ls-files --full-name :/ | grep data2 >actual &&
		echo work/data2 >expect &&
		test_cmp expect actual
	)
'

test_expect_success '$GIT_WORK_TREE overrides $GIT_DIR/common' '
	echo "butdir: repo.but/repos/foo" >.but &&
	echo ../.. >repo.but/repos/foo/commondir &&
	(
		cd work &&
		echo haha >data3 &&
		but --but-dir=../.but --work-tree=. add data3 &&
		but ls-files --full-name -- :/ | grep data3 >actual &&
		echo data3 >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'error out gracefully on invalid $GIT_WORK_TREE' '
	(
		GIT_WORK_TREE=/.invalid/work/tree &&
		export GIT_WORK_TREE &&
		test_expect_code 128 but rev-parse
	)
'

test_expect_success 'refs work with relative butdir and work tree' '
	but init relative &&
	but -C relative cummit --allow-empty -m one &&
	but -C relative cummit --allow-empty -m two &&

	GIT_DIR=relative/.but GIT_WORK_TREE=relative but reset HEAD^ &&

	but -C relative log -1 --format=%s >actual &&
	echo one >expect &&
	test_cmp expect actual
'

test_done
