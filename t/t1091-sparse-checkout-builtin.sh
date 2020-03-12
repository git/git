#!/bin/sh

test_description='sparse checkout builtin tests'

. ./test-lib.sh

list_files() {
	# Do not replace this with 'ls "$1"', as "ls" with BSD-lineage
	# enables "-A" by default for root and ends up including ".git" and
	# such in its output. (Note, though, that running the test suite as
	# root is generally not recommended.)
	(cd "$1" && printf '%s\n' *)
}

check_files() {
	list_files "$1" >actual &&
	shift &&
	printf "%s\n" $@ >expect &&
	test_cmp expect actual
}

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
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
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
	cat >expect <<-\EOF &&
	/*
	!/*/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	test_cmp_config -C repo true core.sparsecheckout &&
	check_files repo a
'

test_expect_success 'git sparse-checkout list after init' '
	git -C repo sparse-checkout list >actual &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	EOF
	test_cmp expect actual
'

test_expect_success 'init with existing sparse-checkout' '
	echo "*folder*" >> repo/.git/info/sparse-checkout &&
	git -C repo sparse-checkout init &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	*folder*
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a folder1 folder2
'

test_expect_success 'clone --sparse' '
	git clone --sparse "file://$(pwd)/repo" clone &&
	git -C clone sparse-checkout list >actual &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	EOF
	test_cmp expect actual &&
	check_files clone a
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
	cat >expect <<-\EOF &&
	/*
	!/*/
	*folder*
	EOF
	git -C repo sparse-checkout list >actual &&
	test_cmp expect actual &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a folder1 folder2
'

test_expect_success 'set sparse-checkout using --stdin' '
	cat >expect <<-\EOF &&
	/*
	!/*/
	/folder1/
	/folder2/
	EOF
	git -C repo sparse-checkout set --stdin <expect &&
	git -C repo sparse-checkout list >actual &&
	test_cmp expect actual &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo "a folder1 folder2"
'

test_expect_success 'add to sparse-checkout' '
	cat repo/.git/info/sparse-checkout >expect &&
	cat >add <<-\EOF &&
	pattern1
	/folder1/
	pattern2
	EOF
	cat add >>expect &&
	git -C repo sparse-checkout add --stdin <add &&
	git -C repo sparse-checkout list >actual &&
	test_cmp expect actual &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo "a folder1 folder2"
'

test_expect_success 'cone mode: match patterns' '
	git -C repo config --worktree core.sparseCheckoutCone true &&
	rm -rf repo/a repo/folder1 repo/folder2 &&
	git -C repo read-tree -mu HEAD 2>err &&
	test_i18ngrep ! "disabling cone patterns" err &&
	git -C repo reset --hard &&
	check_files repo a folder1 folder2
'

test_expect_success 'cone mode: warn on bad pattern' '
	test_when_finished mv sparse-checkout repo/.git/info/ &&
	cp repo/.git/info/sparse-checkout . &&
	echo "!/deep/deeper/*" >>repo/.git/info/sparse-checkout &&
	git -C repo read-tree -mu HEAD 2>err &&
	test_i18ngrep "unrecognized negative pattern" err
'

test_expect_success 'sparse-checkout disable' '
	test_when_finished rm -rf repo/.git/info/sparse-checkout &&
	git -C repo sparse-checkout disable &&
	test_path_is_file repo/.git/info/sparse-checkout &&
	git -C repo config --list >config &&
	test_must_fail git config core.sparseCheckout &&
	check_files repo a deep folder1 folder2
'

test_expect_success 'cone mode: init and set' '
	git -C repo sparse-checkout init --cone &&
	git -C repo config --list >config &&
	test_i18ngrep "core.sparsecheckoutcone=true" config &&
	list_files repo >dir  &&
	echo a >expect &&
	test_cmp expect dir &&
	git -C repo sparse-checkout set deep/deeper1/deepest/ 2>err &&
	test_must_be_empty err &&
	check_files repo a deep &&
	check_files repo/deep a deeper1 &&
	check_files repo/deep/deeper1 a deepest &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	!/deep/*/
	/deep/deeper1/
	!/deep/deeper1/*/
	/deep/deeper1/deepest/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	git -C repo sparse-checkout set --stdin 2>err <<-\EOF &&
	folder1
	folder2
	EOF
	test_must_be_empty err &&
	check_files repo a folder1 folder2
'

test_expect_success 'cone mode: list' '
	cat >expect <<-\EOF &&
	folder1
	folder2
	EOF
	git -C repo sparse-checkout set --stdin <expect &&
	git -C repo sparse-checkout list >actual 2>err &&
	test_must_be_empty err &&
	test_cmp expect actual
'

test_expect_success 'cone mode: set with nested folders' '
	git -C repo sparse-checkout set deep deep/deeper1/deepest 2>err &&
	test_line_count = 0 err &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	EOF
	test_cmp repo/.git/info/sparse-checkout expect
'

test_expect_success 'cone mode: add independent path' '
	git -C repo sparse-checkout set deep/deeper1 &&
	git -C repo sparse-checkout add folder1 &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	!/deep/*/
	/deep/deeper1/
	/folder1/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a deep folder1
'

test_expect_success 'cone mode: add sibling path' '
	git -C repo sparse-checkout set deep/deeper1 &&
	git -C repo sparse-checkout add deep/deeper2 &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	!/deep/*/
	/deep/deeper1/
	/deep/deeper2/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a deep
'

test_expect_success 'cone mode: add parent path' '
	git -C repo sparse-checkout set deep/deeper1 folder1 &&
	git -C repo sparse-checkout add deep &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	/folder1/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a deep folder1
'

test_expect_success 'revert to old sparse-checkout on bad update' '
	test_when_finished git -C repo reset --hard &&
	git -C repo sparse-checkout set deep &&
	echo update >repo/deep/deeper2/a &&
	cp repo/.git/info/sparse-checkout expect &&
	test_must_fail git -C repo sparse-checkout set deep/deeper1 2>err &&
	test_i18ngrep "cannot set sparse-checkout patterns" err &&
	test_cmp repo/.git/info/sparse-checkout expect &&
	check_files repo/deep a deeper1 deeper2
'

test_expect_success 'revert to old sparse-checkout on empty update' '
	git init empty-test &&
	(
		echo >file &&
		git add file &&
		git commit -m "test" &&
		test_must_fail git sparse-checkout set nothing 2>err &&
		test_i18ngrep "Sparse checkout leaves no entry on working directory" err &&
		test_i18ngrep ! ".git/index.lock" err &&
		git sparse-checkout set file
	)
'

test_expect_success 'fail when lock is taken' '
	test_when_finished rm -rf repo/.git/info/sparse-checkout.lock &&
	touch repo/.git/info/sparse-checkout.lock &&
	test_must_fail git -C repo sparse-checkout set deep 2>err &&
	test_i18ngrep "Unable to create .*\.lock" err
'

test_expect_success '.gitignore should not warn about cone mode' '
	git -C repo config --worktree core.sparseCheckoutCone true &&
	echo "**/bin/*" >repo/.gitignore &&
	git -C repo reset --hard 2>err &&
	test_i18ngrep ! "disabling cone patterns" err
'

test_expect_success 'sparse-checkout (init|set|disable) fails with dirty status' '
	git clone repo dirty &&
	echo dirty >dirty/folder1/a &&
	test_must_fail git -C dirty sparse-checkout init &&
	test_must_fail git -C dirty sparse-checkout set /folder2/* /deep/deeper1/* &&
	test_must_fail git -C dirty sparse-checkout disable &&
	git -C dirty reset --hard &&
	git -C dirty sparse-checkout init &&
	git -C dirty sparse-checkout set /folder2/* /deep/deeper1/* &&
	git -C dirty sparse-checkout disable
'

test_expect_success 'cone mode: set with core.ignoreCase=true' '
	git -C repo sparse-checkout init --cone &&
	git -C repo -c core.ignoreCase=true sparse-checkout set folder1 &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/folder1/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a folder1
'

test_expect_success 'interaction with submodules' '
	git clone repo super &&
	(
		cd super &&
		mkdir modules &&
		git submodule add ../repo modules/child &&
		git add . &&
		git commit -m "add submodule" &&
		git sparse-checkout init --cone &&
		git sparse-checkout set folder1
	) &&
	check_files super a folder1 modules &&
	check_files super/modules/child a deep folder1 folder2
'

test_expect_success 'different sparse-checkouts with worktrees' '
	git -C repo worktree add --detach ../worktree &&
	check_files worktree "a deep folder1 folder2" &&
	git -C worktree sparse-checkout init --cone &&
	git -C repo sparse-checkout set folder1 &&
	git -C worktree sparse-checkout set deep/deeper1 &&
	check_files repo a folder1 &&
	check_files worktree a deep
'

test_expect_success 'set using filename keeps file on-disk' '
	git -C repo sparse-checkout set a deep &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/a/
	/deep/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a deep
'

check_read_tree_errors () {
	REPO=$1
	FILES=$2
	ERRORS=$3
	git -C $REPO -c core.sparseCheckoutCone=false read-tree -mu HEAD 2>err &&
	test_must_be_empty err &&
	check_files $REPO "$FILES" &&
	git -C $REPO read-tree -mu HEAD 2>err &&
	if test -z "$ERRORS"
	then
		test_must_be_empty err
	else
		test_i18ngrep "$ERRORS" err
	fi &&
	check_files $REPO $FILES
}

test_expect_success 'pattern-checks: /A/**' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	/folder1/**
	EOF
	check_read_tree_errors repo "a folder1" "disabling cone pattern matching"
'

test_expect_success 'pattern-checks: /A/**/B/' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	/deep/**/deepest
	EOF
	check_read_tree_errors repo "a deep" "disabling cone pattern matching" &&
	check_files repo/deep "deeper1" &&
	check_files repo/deep/deeper1 "deepest"
'

test_expect_success 'pattern-checks: too short' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	/
	EOF
	check_read_tree_errors repo "a" "disabling cone pattern matching"
'
test_expect_success 'pattern-checks: not too short' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	/b/
	EOF
	git -C repo read-tree -mu HEAD 2>err &&
	test_must_be_empty err &&
	check_files repo a
'

test_expect_success 'pattern-checks: trailing "*"' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	/a*
	EOF
	check_read_tree_errors repo "a" "disabling cone pattern matching"
'

test_expect_success 'pattern-checks: starting "*"' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	*eep/
	EOF
	check_read_tree_errors repo "a deep" "disabling cone pattern matching"
'

test_expect_success 'pattern-checks: contained glob characters' '
	for c in "[a]" "\\" "?" "*"
	do
		cat >repo/.git/info/sparse-checkout <<-EOF &&
		/*
		!/*/
		something$c-else/
		EOF
		check_read_tree_errors repo "a" "disabling cone pattern matching"
	done
'

test_expect_success BSLASHPSPEC 'pattern-checks: escaped characters' '
	git clone repo escaped &&
	TREEOID=$(git -C escaped rev-parse HEAD:folder1) &&
	NEWTREE=$(git -C escaped mktree <<-EOF
	$(git -C escaped ls-tree HEAD)
	040000 tree $TREEOID	zbad\\dir
	040000 tree $TREEOID	zdoes*exist
	040000 tree $TREEOID	zglob[!a]?
	EOF
	) &&
	COMMIT=$(git -C escaped commit-tree $NEWTREE -p HEAD) &&
	git -C escaped reset --hard $COMMIT &&
	check_files escaped "a deep folder1 folder2 zbad\\dir zdoes*exist" zglob[!a]? &&
	git -C escaped sparse-checkout init --cone &&
	git -C escaped sparse-checkout set zbad\\dir/bogus "zdoes*not*exist" "zdoes*exist" "zglob[!a]?" &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/zbad\\dir/
	!/zbad\\dir/*/
	/zbad\\dir/bogus/
	/zdoes\*exist/
	/zdoes\*not\*exist/
	/zglob\[!a]\?/
	EOF
	test_cmp expect escaped/.git/info/sparse-checkout &&
	check_read_tree_errors escaped "a zbad\\dir zdoes*exist zglob[!a]?" &&
	git -C escaped ls-tree -d --name-only HEAD >list-expect &&
	git -C escaped sparse-checkout set --stdin <list-expect &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	/folder1/
	/folder2/
	/zbad\\dir/
	/zdoes\*exist/
	/zglob\[!a]\?/
	EOF
	test_cmp expect escaped/.git/info/sparse-checkout &&
	check_files escaped "a deep folder1 folder2 zbad\\dir zdoes*exist" zglob[!a]? &&
	git -C escaped sparse-checkout list >list-actual &&
	test_cmp list-expect list-actual
'

test_expect_success MINGW 'cone mode replaces backslashes with slashes' '
	git -C repo sparse-checkout set deep\\deeper1 &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	/deep/
	!/deep/*/
	/deep/deeper1/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo a deep &&
	check_files repo/deep a deeper1
'

test_done
