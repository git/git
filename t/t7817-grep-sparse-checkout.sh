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
	test_path_is_file sub2/a
'

# The two tests below check a special case: the sparsity patterns exclude '/b'
# and sparse checkout is enabled, but the path exists in the working tree (e.g.
# manually created after `git sparse-checkout init`). In this case, grep should
# skip the file by default, but not with --no-restrict-to-sparse-paths.
test_expect_success 'grep in working tree should honor sparse checkout' '
	cat >expect <<-EOF &&
	a:text
	EOF
	echo "new-text" >b &&
	test_when_finished "rm b" &&
	git grep "text" >actual &&
	test_cmp expect actual
'
test_expect_success 'grep w/ --no-restrict-to-sparse-paths for sparsely excluded but present paths' '
	cat >expect <<-EOF &&
	a:text
	b:new-text
	EOF
	echo "new-text" >b &&
	test_when_finished "rm b" &&
	git --no-restrict-to-sparse-paths grep "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep unmerged file despite not matching sparsity patterns' '
	cat >expect <<-EOF &&
	b:modified-b-in-branchX
	b:modified-b-in-branchY
	EOF
	test_when_finished "test_might_fail git merge --abort && \
			    git checkout master" &&

	git sparse-checkout disable &&
	git checkout -b branchY master &&
	test_commit modified-b-in-branchY b &&
	git checkout -b branchX master &&
	test_commit modified-b-in-branchX b &&

	git sparse-checkout init &&
	test_path_is_missing b &&
	test_must_fail git merge branchY &&
	git grep "modified-b" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached should honor sparse checkout' '
	cat >expect <<-EOF &&
	a:text
	EOF
	git grep --cached "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep <commit-ish> should honor sparse checkout' '
	commit=$(git rev-parse HEAD) &&
	cat >expect_commit <<-EOF &&
	$commit:a:text
	EOF
	cat >expect_tag-to-commit <<-EOF &&
	tag-to-commit:a:text
	EOF
	git grep "text" $commit >actual_commit &&
	test_cmp expect_commit actual_commit &&
	git grep "text" tag-to-commit >actual_tag-to-commit &&
	test_cmp expect_tag-to-commit actual_tag-to-commit
'

test_expect_success 'grep <tree-ish> should ignore sparsity patterns' '
	commit=$(git rev-parse HEAD) &&
	tree=$(git rev-parse HEAD^{tree}) &&
	cat >expect_tree <<-EOF &&
	$tree:a:text
	$tree:b:text
	$tree:dir/c:text
	EOF
	cat >expect_tag-to-tree <<-EOF &&
	tag-to-tree:a:text
	tag-to-tree:b:text
	tag-to-tree:dir/c:text
	EOF
	git grep "text" $tree >actual_tree &&
	test_cmp expect_tree actual_tree &&
	git grep "text" tag-to-tree >actual_tag-to-tree &&
	test_cmp expect_tag-to-tree actual_tag-to-tree
'

# Note that sub2/ is present in the worktree but it is excluded by the sparsity
# patterns, so grep should only recurse into it with --no-restrict-to-sparse-paths.
test_expect_success 'grep --recurse-submodules should honor sparse checkout in submodule' '
	cat >expect <<-EOF &&
	a:text
	sub/B/b:text
	EOF
	git grep --recurse-submodules "text" >actual &&
	test_cmp expect actual
'
test_expect_success 'grep --recurse-submodules should search in excluded submodules w/ --no-restrict-to-sparse-paths' '
	cat >expect <<-EOF &&
	a:text
	sub/B/b:text
	sub2/a:text
	EOF
	git --no-restrict-to-sparse-paths grep --recurse-submodules "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules --cached should honor sparse checkout in submodule' '
	cat >expect <<-EOF &&
	a:text
	sub/B/b:text
	EOF
	git grep --recurse-submodules --cached "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules <commit-ish> should honor sparse checkout in submodule' '
	commit=$(git rev-parse HEAD) &&
	cat >expect_commit <<-EOF &&
	$commit:a:text
	$commit:sub/B/b:text
	EOF
	cat >expect_tag-to-commit <<-EOF &&
	tag-to-commit:a:text
	tag-to-commit:sub/B/b:text
	EOF
	git grep --recurse-submodules "text" $commit >actual_commit &&
	test_cmp expect_commit actual_commit &&
	git grep --recurse-submodules "text" tag-to-commit >actual_tag-to-commit &&
	test_cmp expect_tag-to-commit actual_tag-to-commit
'

for cmd in 'git --no-restrict-to-sparse-paths grep' \
	   'git -c sparse.restrictCmds=false grep' \
	   'git -c sparse.restrictCmds=true --no-restrict-to-sparse-paths grep'
do

	test_expect_success "$cmd --cached should ignore sparsity patterns" '
		cat >expect <<-EOF &&
		a:text
		b:text
		dir/c:text
		EOF
		$cmd --cached "text" >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd <commit-ish> should ignore sparsity patterns" '
		commit=$(git rev-parse HEAD) &&
		cat >expect_commit <<-EOF &&
		$commit:a:text
		$commit:b:text
		$commit:dir/c:text
		EOF
		cat >expect_tag-to-commit <<-EOF &&
		tag-to-commit:a:text
		tag-to-commit:b:text
		tag-to-commit:dir/c:text
		EOF
		$cmd "text" $commit >actual_commit &&
		test_cmp expect_commit actual_commit &&
		$cmd "text" tag-to-commit >actual_tag-to-commit &&
		test_cmp expect_tag-to-commit actual_tag-to-commit
	'
done

test_expect_success 'grep --recurse-submodules --cached w/ --no-restrict-to-sparse-paths' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	sub/A/a:text
	sub/B/b:text
	sub2/a:text
	EOF
	git --no-restrict-to-sparse-paths grep --recurse-submodules --cached \
		"text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules <commit-ish> w/ --no-restrict-to-sparse-paths' '
	commit=$(git rev-parse HEAD) &&
	cat >expect_commit <<-EOF &&
	$commit:a:text
	$commit:b:text
	$commit:dir/c:text
	$commit:sub/A/a:text
	$commit:sub/B/b:text
	$commit:sub2/a:text
	EOF
	cat >expect_tag-to-commit <<-EOF &&
	tag-to-commit:a:text
	tag-to-commit:b:text
	tag-to-commit:dir/c:text
	tag-to-commit:sub/A/a:text
	tag-to-commit:sub/B/b:text
	tag-to-commit:sub2/a:text
	EOF
	git --no-restrict-to-sparse-paths grep --recurse-submodules "text" \
		$commit >actual_commit &&
	test_cmp expect_commit actual_commit &&
	git --no-restrict-to-sparse-paths grep --recurse-submodules "text" \
		tag-to-commit >actual_tag-to-commit &&
	test_cmp expect_tag-to-commit actual_tag-to-commit
'

test_expect_success 'should respect the sparse.restrictCmds values from submodules' '
	cat >expect <<-EOF &&
	a:text
	sub/A/a:text
	sub/B/b:text
	EOF
	test_config -C sub sparse.restrictCmds false &&
	git grep --cached --recurse-submodules "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'should propagate --[no]-restrict-to-sparse-paths to submodules' '
	cat >expect <<-EOF &&
	a:text
	b:text
	dir/c:text
	sub/A/a:text
	sub/B/b:text
	sub2/a:text
	EOF
	test_config -C sub sparse.restrictCmds true &&
	git --no-restrict-to-sparse-paths grep --cached --recurse-submodules "text" >actual &&
	test_cmp expect actual
'

for opt in '--untracked' '--no-index'
do
	test_expect_success "--[no]-restrict-to-sparse-paths and $opt are incompatible" "
		test_must_fail git --restrict-to-sparse-paths grep $opt . 2>actual &&
		test_i18ngrep 'restrict-to-sparse-paths is incompatible with' actual
	"
done

test_done
