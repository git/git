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

	git init sub &&
	(
		cd sub &&
		mkdir A B &&
		echo "text" >A/a &&
		echo "text" >B/b &&
		git add A B &&
		git commit -m sub &&
		git sparse-checkout init --cone &&
		git sparse-checkout set B
	) &&

	git init sub2 &&
	(
		cd sub2 &&
		echo "text" >a &&
		git add a &&
		git commit -m sub2
	) &&

	git submodule add ./sub &&
	git submodule add ./sub2 &&
	git add a b dir &&
	git commit -m super &&
	git sparse-checkout init --no-cone &&
	git sparse-checkout set "/*" "!b" "!/*/" "sub" &&

	git tag -am tag-to-commit tag-to-commit HEAD &&
	tree=$(git rev-parse HEAD^{tree}) &&
	git tag -am tag-to-tree tag-to-tree $tree &&

	test_path_is_missing b &&
	test_path_is_missing dir &&
	test_path_is_missing sub/A &&
	test_path_is_file a &&
	test_path_is_file sub/B/b &&
	test_path_is_file sub2/a &&
	git branch -m main
'

# The test below covers a special case: the sparsity patterns exclude '/b' and
# sparse checkout is enabled, but the path exists in the working tree (e.g.
# manually created after `git sparse-checkout init`).  Although b is marked
# as SKIP_WORKTREE, git grep should notice it IS present in the worktree and
# report it.
test_expect_success 'working tree grep honors sparse checkout' '
	cat >expect <<-EOF &&
	a:text
	b:new-text
	EOF
	test_when_finished "rm -f b" &&
	echo "new-text" >b &&
	git grep "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep searches unmerged file despite not matching sparsity patterns' '
	cat >expect <<-EOF &&
	b:modified-b-in-branchX
	b:modified-b-in-branchY
	EOF
	test_when_finished "test_might_fail git merge --abort && \
			    git checkout main && git sparse-checkout init" &&

	git sparse-checkout disable &&
	git checkout -b branchY main &&
	test_commit modified-b-in-branchY b &&
	git checkout -b branchX main &&
	test_commit modified-b-in-branchX b &&

	git sparse-checkout init &&
	test_path_is_missing b &&
	test_must_fail git merge branchY &&
	git grep "modified-b" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached searches entries with the SKIP_WORKTREE bit' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	EOF
	git grep --cached "text" >actual &&
	test_cmp expect actual
'

# Note that sub2/ is present in the worktree but it is excluded by the sparsity
# patterns.  We also explicitly mark it as SKIP_WORKTREE in case it got cleared
# by previous git commands.  Thus sub2 starts as SKIP_WORKTREE but since it is
# present in the working tree, grep should recurse into it.
test_expect_success 'grep --recurse-submodules honors sparse checkout in submodule' '
	cat >expect <<-EOF &&
	a:text
	sub/B/b:text
	sub2/a:text
	EOF
	git update-index --skip-worktree sub2 &&
	git grep --recurse-submodules "text" >actual &&
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
	git grep --recurse-submodules --cached "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'working tree grep does not search the index with CE_VALID and SKIP_WORKTREE' '
	cat >expect <<-EOF &&
	a:text
	EOF
	test_when_finished "git update-index --no-assume-unchanged b" &&
	git update-index --assume-unchanged b &&
	git grep text >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached searches index entries with both CE_VALID and SKIP_WORKTREE' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	EOF
	test_when_finished "git update-index --no-assume-unchanged b" &&
	git update-index --assume-unchanged b &&
	git grep --cached text >actual &&
	test_cmp expect actual
'

test_done
