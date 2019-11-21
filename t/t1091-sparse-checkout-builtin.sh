#!/bin/sh

test_description='sparse checkout builtin tests'

. ./test-lib.sh

test_expect_success 'setup' '
	git init repo &&
	(
		cd repo &&
		echo "initial" >a &&
		mkdir folder1 folder2 deep &&
		mkdir deep/deeper1 deep/deeper2 &&
		mkdir deep/deeper1/deepest &&
		cp a folder1 &&
		cp a folder2 &&
		cp a deep &&
		cp a deep/deeper1 &&
		cp a deep/deeper2 &&
		cp a deep/deeper1/deepest &&
		git add . &&
		git commit -m "initial commit"
	)
'

test_expect_success 'git sparse-checkout list (empty)' '
	git -C repo sparse-checkout list >list 2>err &&
	test_must_be_empty list &&
	test_i18ngrep "this worktree is not sparse (sparse-checkout file may not exist)" err
'

test_expect_success 'git sparse-checkout list (populated)' '
	test_when_finished rm -f repo/.git/info/sparse-checkout &&
	cat >repo/.git/info/sparse-checkout <<-EOF &&
		/folder1/*
		/deep/
		**/a
		!*bin*
	EOF
	cp repo/.git/info/sparse-checkout expect &&
	git -C repo sparse-checkout list >list &&
	test_cmp expect list
'

test_expect_success 'git sparse-checkout init' '
	git -C repo sparse-checkout init &&
	cat >expect <<-EOF &&
		/*
		!/*/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	test_cmp_config -C repo true core.sparsecheckout &&
	ls repo >dir  &&
	echo a >expect &&
	test_cmp expect dir
'

test_expect_success 'git sparse-checkout list after init' '
	git -C repo sparse-checkout list >actual &&
	cat >expect <<-EOF &&
		/*
		!/*/
	EOF
	test_cmp expect actual
'

test_expect_success 'init with existing sparse-checkout' '
	echo "*folder*" >> repo/.git/info/sparse-checkout &&
	git -C repo sparse-checkout init &&
	cat >expect <<-EOF &&
		/*
		!/*/
		*folder*
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	ls repo >dir  &&
	cat >expect <<-EOF &&
		a
		folder1
		folder2
	EOF
	test_cmp expect dir
'

test_expect_success 'clone --sparse' '
	git clone --sparse repo clone &&
	git -C clone sparse-checkout list >actual &&
	cat >expect <<-EOF &&
		/*
		!/*/
	EOF
	test_cmp expect actual &&
	ls clone >dir &&
	echo a >expect &&
	test_cmp expect dir
'

test_expect_success 'set enables config' '
	git init empty-config &&
	(
		cd empty-config &&
		test_commit test file &&
		test_path_is_missing .git/config.worktree &&
		test_must_fail git sparse-checkout set nothing &&
		test_path_is_file .git/config.worktree &&
		test_must_fail git config core.sparseCheckout &&
		git sparse-checkout set "/*" &&
		test_cmp_config true core.sparseCheckout
	)
'

test_expect_success 'set sparse-checkout using builtin' '
	git -C repo sparse-checkout set "/*" "!/*/" "*folder*" &&
	cat >expect <<-EOF &&
		/*
		!/*/
		*folder*
	EOF
	git -C repo sparse-checkout list >actual &&
	test_cmp expect actual &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	ls repo >dir  &&
	cat >expect <<-EOF &&
		a
		folder1
		folder2
	EOF
	test_cmp expect dir
'

test_expect_success 'set sparse-checkout using --stdin' '
	cat >expect <<-EOF &&
		/*
		!/*/
		/folder1/
		/folder2/
	EOF
	git -C repo sparse-checkout set --stdin <expect &&
	git -C repo sparse-checkout list >actual &&
	test_cmp expect actual &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	ls repo >dir  &&
	cat >expect <<-EOF &&
		a
		folder1
		folder2
	EOF
	test_cmp expect dir
'

test_expect_success 'cone mode: match patterns' '
	git -C repo config --worktree core.sparseCheckoutCone true &&
	rm -rf repo/a repo/folder1 repo/folder2 &&
	git -C repo read-tree -mu HEAD &&
	git -C repo reset --hard &&
	ls repo >dir  &&
	cat >expect <<-EOF &&
		a
		folder1
		folder2
	EOF
	test_cmp expect dir
'

test_expect_success 'sparse-checkout disable' '
	git -C repo sparse-checkout disable &&
	test_path_is_missing repo/.git/info/sparse-checkout &&
	git -C repo config --list >config &&
	test_must_fail git config core.sparseCheckout &&
	ls repo >dir &&
	cat >expect <<-EOF &&
		a
		deep
		folder1
		folder2
	EOF
	test_cmp expect dir
'

test_done
