#!/bin/sh

test_description='grep in sparse checkout

This test creates a repo with the following structure:

.
|-- a
|-- b
|-- dir
|   `-- c
|-- sub
|   |-- A
|   |   `-- a
|   `-- B
|       `-- b
`-- sub2
    `-- a

Where the outer repository has non-cone mode sparsity patterns, sub is a
submodule with cone mode sparsity patterns and sub2 is a submodule that is
excluded by the superproject sparsity patterns. The resulting sparse checkout
should leave the following structure in the working tree:

.
|-- a
|-- sub
|   `-- B
|       `-- b
`-- sub2
    `-- a

But note that sub2 should have the SKIP_WORKTREE bit set.
'

. ./test-lib.sh

test_expect_success 'setup' '
	echo "text" >a &&
	echo "text" >b &&
	mkdir dir &&
	echo "text" >dir/c &&

	but init sub &&
	(
		cd sub &&
		mkdir A B &&
		echo "text" >A/a &&
		echo "text" >B/b &&
		but add A B &&
		but cummit -m sub &&
		but sparse-checkout init --cone &&
		but sparse-checkout set B
	) &&

	but init sub2 &&
	(
		cd sub2 &&
		echo "text" >a &&
		but add a &&
		but cummit -m sub2
	) &&

	but submodule add ./sub &&
	but submodule add ./sub2 &&
	but add a b dir &&
	but cummit -m super &&
	but sparse-checkout init --no-cone &&
	but sparse-checkout set "/*" "!b" "!/*/" "sub" &&

	but tag -am tag-to-cummit tag-to-commit HEAD &&
	tree=$(but rev-parse HEAD^{tree}) &&
	but tag -am tag-to-tree tag-to-tree $tree &&

	test_path_is_missing b &&
	test_path_is_missing dir &&
	test_path_is_missing sub/A &&
	test_path_is_file a &&
	test_path_is_file sub/B/b &&
	test_path_is_file sub2/a &&
	but branch -m main
'

# The test below covers a special case: the sparsity patterns exclude '/b' and
# sparse checkout is enabled, but the path exists in the working tree (e.g.
# manually created after `but sparse-checkout init`).  Although b is marked
# as SKIP_WORKTREE, but grep should notice it IS present in the worktree and
# report it.
test_expect_success 'working tree grep honors sparse checkout' '
	cat >expect <<-EOF &&
	a:text
	b:new-text
	EOF
	test_when_finished "rm -f b" &&
	echo "new-text" >b &&
	but grep "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep searches unmerged file despite not matching sparsity patterns' '
	cat >expect <<-EOF &&
	b:modified-b-in-branchX
	b:modified-b-in-branchY
	EOF
	test_when_finished "test_might_fail but merge --abort && \
			    but checkout main && but sparse-checkout init" &&

	but sparse-checkout disable &&
	but checkout -b branchY main &&
	test_cummit modified-b-in-branchY b &&
	but checkout -b branchX main &&
	test_cummit modified-b-in-branchX b &&

	but sparse-checkout init &&
	test_path_is_missing b &&
	test_must_fail but merge branchY &&
	but grep "modified-b" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached searches entries with the SKIP_WORKTREE bit' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	EOF
	but grep --cached "text" >actual &&
	test_cmp expect actual
'

# Note that sub2/ is present in the worktree but it is excluded by the sparsity
# patterns.  We also explicitly mark it as SKIP_WORKTREE in case it got cleared
# by previous but commands.  Thus sub2 starts as SKIP_WORKTREE but since it is
# present in the working tree, grep should recurse into it.
test_expect_success 'grep --recurse-submodules honors sparse checkout in submodule' '
	cat >expect <<-EOF &&
	a:text
	sub/B/b:text
	sub2/a:text
	EOF
	but update-index --skip-worktree sub2 &&
	but grep --recurse-submodules "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules --cached searches entries with the SKIP_WORKTREE bit' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	sub/A/a:text
	sub/B/b:text
	sub2/a:text
	EOF
	but grep --recurse-submodules --cached "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'working tree grep does not search the index with CE_VALID and SKIP_WORKTREE' '
	cat >expect <<-EOF &&
	a:text
	EOF
	test_when_finished "but update-index --no-assume-unchanged b" &&
	but update-index --assume-unchanged b &&
	but grep text >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached searches index entries with both CE_VALID and SKIP_WORKTREE' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	EOF
	test_when_finished "but update-index --no-assume-unchanged b" &&
	but update-index --assume-unchanged b &&
	but grep --cached text >actual &&
	test_cmp expect actual
'

test_done
