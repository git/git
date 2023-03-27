#!/bin/sh

test_description='sparse checkout builtin tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

GIT_TEST_SPLIT_INDEX=false
export GIT_TEST_SPLIT_INDEX

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

test_expect_success 'git sparse-checkout list (not sparse)' '
	test_must_fail git -C repo sparse-checkout list >list 2>err &&
	test_must_be_empty list &&
	test_i18ngrep "this worktree is not sparse" err
'

test_expect_success 'git sparse-checkout list (not sparse)' '
	git -C repo sparse-checkout set &&
	rm repo/.git/info/sparse-checkout &&
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
	git -C repo sparse-checkout init --no-cone &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	EOF
	test_cmp expect repo/.git/info/sparse-checkout &&
	test_cmp_config -C repo true core.sparsecheckout &&
	check_files repo a
'

test_expect_success 'git sparse-checkout init in empty repo' '
	test_when_finished rm -rf empty-repo blank-template &&
	git init --template= empty-repo &&
	git -C empty-repo sparse-checkout init
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
	git -C clone sparse-checkout reapply --no-cone &&
	git -C clone sparse-checkout list >actual &&
	cat >expect <<-\EOF &&
	/*
	!/*/
	EOF
	test_cmp expect actual &&
	check_files clone a
'

test_expect_success 'switching to cone mode with non-cone mode patterns' '
	git init bad-patterns &&
	(
		cd bad-patterns &&
		git sparse-checkout init --no-cone &&
		git sparse-checkout add dir &&
		git config --worktree core.sparseCheckoutCone true &&
		test_must_fail git sparse-checkout add dir 2>err &&
		grep "existing sparse-checkout patterns do not use cone mode" err
	)
'

test_expect_success 'interaction with clone --no-checkout (unborn index)' '
	git clone --no-checkout "file://$(pwd)/repo" clone_no_checkout &&
	git -C clone_no_checkout sparse-checkout init --cone &&
	git -C clone_no_checkout sparse-checkout set folder1 &&

	git -C clone_no_checkout sparse-checkout list >actual &&
	cat >expect <<-\EOF &&
	folder1
	EOF
	test_cmp expect actual &&

	# nothing checked out, expect "No such file or directory"
	! ls clone_no_checkout/* >actual &&
	test_must_be_empty actual &&
	test_path_is_missing clone_no_checkout/.git/index &&

	# No branch is checked out until we manually switch to one
	git -C clone_no_checkout switch main &&
	test_path_is_file clone_no_checkout/.git/index &&
	check_files clone_no_checkout a folder1
'

test_expect_success 'set enables config' '
	git init worktree-config &&
	(
		cd worktree-config &&
		test_commit test file &&
		test_path_is_missing .git/config.worktree &&
		git sparse-checkout set nothing &&
		test_path_is_file .git/config.worktree &&
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
	cat repo/.git/info/sparse-checkout >old &&
	test_when_finished cp old repo/.git/info/sparse-checkout &&
	cat >add <<-\EOF &&
	pattern1
	/folder1/
	pattern2
	EOF
	cat old >expect &&
	cat add >>expect &&
	git -C repo sparse-checkout add --stdin <add &&
	git -C repo sparse-checkout list >actual &&
	test_cmp expect actual &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo "a folder1 folder2"
'

test_expect_success 'worktree: add copies sparse-checkout patterns' '
	cat repo/.git/info/sparse-checkout >old &&
	test_when_finished cp old repo/.git/info/sparse-checkout &&
	test_when_finished git -C repo worktree remove ../worktree &&
	git -C repo sparse-checkout set --no-cone "/*" &&
	git -C repo worktree add --quiet ../worktree 2>err &&
	test_must_be_empty err &&
	new="$(git -C worktree rev-parse --git-path info/sparse-checkout)" &&
	test_path_is_file "$new" &&
	test_cmp repo/.git/info/sparse-checkout "$new" &&
	git -C worktree sparse-checkout set --cone &&
	test_cmp_config -C worktree true core.sparseCheckoutCone &&
	test_must_fail git -C repo core.sparseCheckoutCone
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
	echo "!/deep/deeper/*/" >>repo/.git/info/sparse-checkout &&
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

test_expect_success 'sparse-index enabled and disabled' '
	git -C repo sparse-checkout init --cone --sparse-index &&
	test_cmp_config -C repo true index.sparse &&
	git -C repo ls-files --sparse >sparse &&
	git -C repo sparse-checkout disable &&
	git -C repo ls-files --sparse >full &&

	cat >expect <<-\EOF &&
	@@ -1,4 +1,7 @@
	 a
	-deep/
	-folder1/
	-folder2/
	+deep/a
	+deep/deeper1/a
	+deep/deeper1/deepest/a
	+deep/deeper2/a
	+folder1/a
	+folder2/a
	EOF

	diff -u sparse full | tail -n +3 >actual &&
	test_cmp expect actual &&

	git -C repo config --list >config &&
	test_cmp_config -C repo false index.sparse
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

test_expect_success 'not-up-to-date does not block rest of sparsification' '
	test_when_finished git -C repo sparse-checkout disable &&
	test_when_finished git -C repo reset --hard &&
	git -C repo sparse-checkout set deep &&

	echo update >repo/deep/deeper2/a &&
	cp repo/.git/info/sparse-checkout expect &&
	test_write_lines "!/deep/*/" "/deep/deeper1/" >>expect &&

	git -C repo sparse-checkout set deep/deeper1 2>err &&

	test_i18ngrep "The following paths are not up to date" err &&
	test_cmp expect repo/.git/info/sparse-checkout &&
	check_files repo/deep a deeper1 deeper2 &&
	check_files repo/deep/deeper1 a deepest &&
	check_files repo/deep/deeper1/deepest a &&
	check_files repo/deep/deeper2 a
'

test_expect_success 'revert to old sparse-checkout on empty update' '
	git init empty-test &&
	(
		echo >file &&
		git add file &&
		git commit -m "test" &&
		git sparse-checkout set nothing 2>err &&
		test_i18ngrep ! "Sparse checkout leaves no entry on working directory" err &&
		test_i18ngrep ! ".git/index.lock" err &&
		git sparse-checkout set --no-cone file
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

test_expect_success 'sparse-checkout (init|set|disable) warns with dirty status' '
	git clone repo dirty &&
	echo dirty >dirty/folder1/a &&

	git -C dirty sparse-checkout init --no-cone 2>err &&
	test_i18ngrep "warning.*The following paths are not up to date" err &&

	git -C dirty sparse-checkout set /folder2/* /deep/deeper1/* 2>err &&
	test_i18ngrep "warning.*The following paths are not up to date" err &&
	test_path_is_file dirty/folder1/a &&

	git -C dirty sparse-checkout disable 2>err &&
	test_must_be_empty err &&

	git -C dirty reset --hard &&
	git -C dirty sparse-checkout init --no-cone &&
	git -C dirty sparse-checkout set /folder2/* /deep/deeper1/* &&
	test_path_is_missing dirty/folder1/a &&
	git -C dirty sparse-checkout disable &&
	test_path_is_file dirty/folder1/a
'

test_expect_success 'sparse-checkout (init|set|disable) warns with unmerged status' '
	git clone repo unmerged &&

	cat >input <<-EOF &&
	0 $ZERO_OID	folder1/a
	100644 $(git -C unmerged rev-parse HEAD:folder1/a) 1	folder1/a
	EOF
	git -C unmerged update-index --index-info <input &&

	git -C unmerged sparse-checkout init --no-cone 2>err &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&

	git -C unmerged sparse-checkout set /folder2/* /deep/deeper1/* 2>err &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&
	test_path_is_file dirty/folder1/a &&

	git -C unmerged sparse-checkout disable 2>err &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&

	git -C unmerged reset --hard &&
	git -C unmerged sparse-checkout init --no-cone &&
	git -C unmerged sparse-checkout set /folder2/* /deep/deeper1/* &&
	git -C unmerged sparse-checkout disable
'

test_expect_failure 'sparse-checkout reapply' '
	git clone repo tweak &&

	echo dirty >tweak/deep/deeper2/a &&

	cat >input <<-EOF &&
	0 $ZERO_OID	folder1/a
	100644 $(git -C tweak rev-parse HEAD:folder1/a) 1	folder1/a
	EOF
	git -C tweak update-index --index-info <input &&

	git -C tweak sparse-checkout init --cone 2>err &&
	test_i18ngrep "warning.*The following paths are not up to date" err &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&

	git -C tweak sparse-checkout set folder2 deep/deeper1 2>err &&
	test_i18ngrep "warning.*The following paths are not up to date" err &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&

	git -C tweak sparse-checkout reapply 2>err &&
	test_i18ngrep "warning.*The following paths are not up to date" err &&
	test_path_is_file tweak/deep/deeper2/a &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&
	test_path_is_file tweak/folder1/a &&

	git -C tweak checkout HEAD deep/deeper2/a &&
	git -C tweak sparse-checkout reapply 2>err &&
	test_i18ngrep ! "warning.*The following paths are not up to date" err &&
	test_path_is_missing tweak/deep/deeper2/a &&
	test_i18ngrep "warning.*The following paths are unmerged" err &&
	test_path_is_file tweak/folder1/a &&

	# NEEDSWORK: We are asking to update a file outside of the
	# sparse-checkout cone, but this is no longer allowed.
	git -C tweak add folder1/a &&
	git -C tweak sparse-checkout reapply 2>err &&
	test_must_be_empty err &&
	test_path_is_missing tweak/deep/deeper2/a &&
	test_path_is_missing tweak/folder1/a &&

	git -C tweak sparse-checkout disable
'

test_expect_success 'reapply can handle config options' '
	git -C repo sparse-checkout init --cone --no-sparse-index &&
	git -C repo config --worktree --list >actual &&
	cat >expect <<-\EOF &&
	core.sparsecheckout=true
	core.sparsecheckoutcone=true
	index.sparse=false
	EOF
	test_cmp expect actual &&

	git -C repo sparse-checkout reapply --no-cone --no-sparse-index &&
	git -C repo config --worktree --list >actual &&
	cat >expect <<-\EOF &&
	core.sparsecheckout=true
	core.sparsecheckoutcone=false
	index.sparse=false
	EOF
	test_cmp expect actual &&

	git -C repo sparse-checkout reapply --cone --sparse-index &&
	git -C repo config --worktree --list >actual &&
	cat >expect <<-\EOF &&
	core.sparsecheckout=true
	core.sparsecheckoutcone=true
	index.sparse=true
	EOF
	test_cmp expect actual &&

	git -C repo sparse-checkout disable
'

test_expect_success 'cone mode: set with core.ignoreCase=true' '
	rm repo/.git/info/sparse-checkout &&
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

test_expect_success 'setup submodules' '
	git clone repo super &&
	(
		cd super &&
		mkdir modules &&
		git -c protocol.file.allow=always \
			submodule add ../repo modules/child &&
		git add . &&
		git commit -m "add submodule" &&
		git sparse-checkout init --cone &&
		git sparse-checkout set folder1
	)
'

test_expect_success 'interaction with submodules' '
	check_files super a folder1 modules &&
	check_files super/modules/child a deep folder1 folder2
'

test_expect_success 'check-rules interaction with submodules' '
	git -C super ls-tree --name-only -r HEAD >all-files &&
	git -C super sparse-checkout check-rules >check-rules-matches <all-files &&

	test_i18ngrep ! "modules/" check-rules-matches &&
	test_i18ngrep "folder1/" check-rules-matches
'

test_expect_success 'different sparse-checkouts with worktrees' '
	git -C repo sparse-checkout set --cone deep folder1 &&
	git -C repo worktree add --detach ../worktree &&
	check_files worktree "a deep folder1" &&
	git -C repo sparse-checkout set --cone folder1 &&
	git -C worktree sparse-checkout set --cone deep/deeper1 &&
	check_files repo "a folder1" &&
	check_files worktree "a deep"
'

test_expect_success 'set using filename keeps file on-disk' '
	git -C repo sparse-checkout set --skip-checks a deep &&
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

test_expect_success 'pattern-checks: non directory pattern' '
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/deep/deeper1/a
	EOF
	check_read_tree_errors repo deep "disabling cone pattern matching" &&
	check_files repo/deep deeper1 &&
	check_files repo/deep/deeper1 a
'

test_expect_success 'pattern-checks: contained glob characters' '
	for c in "[a]" "\\" "?" "*"
	do
		cat >repo/.git/info/sparse-checkout <<-EOF &&
		/*
		!/*/
		something$c-else/
		EOF
		check_read_tree_errors repo "a" "disabling cone pattern matching" || return 1
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
	git -C escaped sparse-checkout set --skip-checks zbad\\dir/bogus "zdoes*not*exist" "zdoes*exist" "zglob[!a]?" &&
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

test_expect_success 'cone mode clears ignored subdirectories' '
	rm repo/.git/info/sparse-checkout &&

	git -C repo sparse-checkout init --cone &&
	git -C repo sparse-checkout set deep/deeper1 &&

	cat >repo/.gitignore <<-\EOF &&
	obj/
	*.o
	EOF

	git -C repo add .gitignore &&
	git -C repo commit -m ".gitignore" &&

	mkdir -p repo/obj repo/folder1/obj repo/deep/deeper2/obj &&
	for file in folder1/obj/a obj/a folder1/file.o folder1.o \
		    deep/deeper2/obj/a deep/deeper2/file.o file.o
	do
		echo ignored >repo/$file || return 1
	done &&

	git -C repo status --porcelain=v2 >out &&
	test_must_be_empty out &&

	git -C repo sparse-checkout reapply &&
	test_path_is_missing repo/folder1 &&
	test_path_is_missing repo/deep/deeper2 &&
	test_path_is_dir repo/obj &&
	test_path_is_file repo/file.o &&

	git -C repo status --porcelain=v2 >out &&
	test_must_be_empty out &&

	git -C repo sparse-checkout set deep/deeper2 &&
	test_path_is_missing repo/deep/deeper1 &&
	test_path_is_dir repo/deep/deeper2 &&
	test_path_is_dir repo/obj &&
	test_path_is_file repo/file.o &&

	>repo/deep/deeper2/ignored.o &&
	>repo/deep/deeper2/untracked &&

	# When an untracked file is in the way, all untracked files
	# (even ignored files) are preserved.
	git -C repo sparse-checkout set folder1 2>err &&
	grep "contains untracked files" err &&
	test_path_is_file repo/deep/deeper2/ignored.o &&
	test_path_is_file repo/deep/deeper2/untracked &&

	# The rest of the cone matches expectation
	test_path_is_missing repo/deep/deeper1 &&
	test_path_is_dir repo/obj &&
	test_path_is_file repo/file.o &&

	git -C repo status --porcelain=v2 >out &&
	echo "? deep/deeper2/untracked" >expect &&
	test_cmp expect out
'

test_expect_success 'malformed cone-mode patterns' '
	git -C repo sparse-checkout init --cone &&
	mkdir -p repo/foo/bar &&
	touch repo/foo/bar/x repo/foo/y &&
	cat >repo/.git/info/sparse-checkout <<-\EOF &&
	/*
	!/*/
	/foo/
	!/foo/*/
	/foo/\*/
	EOF

	# Listing the patterns will notice the duplicate pattern and
	# emit a warning. It will list the patterns directly instead
	# of using the cone-mode translation to a set of directories.
	git -C repo sparse-checkout list >actual 2>err &&
	test_cmp repo/.git/info/sparse-checkout actual &&
	grep "warning: your sparse-checkout file may have issues: pattern .* is repeated" err &&
	grep "warning: disabling cone pattern matching" err
'

test_expect_success 'set from subdir pays attention to prefix' '
	git -C repo sparse-checkout disable &&
	git -C repo/deep sparse-checkout set --cone deeper2 ../folder1 &&

	git -C repo sparse-checkout list >actual &&

	cat >expect <<-\EOF &&
	deep/deeper2
	folder1
	EOF
	test_cmp expect actual
'

test_expect_success 'add from subdir pays attention to prefix' '
	git -C repo sparse-checkout set --cone deep/deeper2 &&
	git -C repo/deep sparse-checkout add deeper1/deepest ../folder1 &&

	git -C repo sparse-checkout list >actual &&

	cat >expect <<-\EOF &&
	deep/deeper1/deepest
	deep/deeper2
	folder1
	EOF
	test_cmp expect actual
'

test_expect_success 'set from subdir in non-cone mode throws an error' '
	git -C repo sparse-checkout disable &&
	test_must_fail git -C repo/deep sparse-checkout set --no-cone deeper2 ../folder1 2>error &&

	grep "run from the toplevel directory in non-cone mode" error
'

test_expect_success 'set from subdir in non-cone mode throws an error' '
	git -C repo sparse-checkout set --no-cone deep/deeper2 &&
	test_must_fail git -C repo/deep sparse-checkout add deeper1/deepest ../folder1 2>error &&

	grep "run from the toplevel directory in non-cone mode" error
'

test_expect_success 'by default, cone mode will error out when passed files' '
	git -C repo sparse-checkout reapply --cone &&
	test_must_fail git -C repo sparse-checkout add .gitignore 2>error &&

	grep ".gitignore.*is not a directory" error
'

test_expect_success 'by default, non-cone mode will warn on individual files' '
	git -C repo sparse-checkout reapply --no-cone &&
	git -C repo sparse-checkout add .gitignore 2>warning &&

	grep "pass a leading slash before paths.*if you want a single file" warning
'

test_expect_success 'setup bare repo' '
	git clone --bare "file://$(pwd)/repo" bare
'
test_expect_success 'list fails outside work tree' '
	test_must_fail git -C bare sparse-checkout list 2>err &&
	test_i18ngrep "this operation must be run in a work tree" err
'

test_expect_success 'add fails outside work tree' '
	test_must_fail git -C bare sparse-checkout add deeper 2>err &&
	test_i18ngrep "this operation must be run in a work tree" err
'

test_expect_success 'set fails outside work tree' '
	test_must_fail git -C bare sparse-checkout set deeper 2>err &&
	test_i18ngrep "this operation must be run in a work tree" err
'

test_expect_success 'init fails outside work tree' '
	test_must_fail git -C bare sparse-checkout init 2>err &&
	test_i18ngrep "this operation must be run in a work tree" err
'

test_expect_success 'reapply fails outside work tree' '
	test_must_fail git -C bare sparse-checkout reapply 2>err &&
	test_i18ngrep "this operation must be run in a work tree" err
'

test_expect_success 'disable fails outside work tree' '
	test_must_fail git -C bare sparse-checkout disable 2>err &&
	test_i18ngrep "this operation must be run in a work tree" err
'

test_expect_success 'setup clean' '
	git -C repo clean -fdx
'

test_expect_success 'check-rules cone mode' '
	cat >rules <<-\EOF &&
	folder1
	deep/deeper1/deepest
	EOF

	git -C bare ls-tree -r --name-only HEAD >all-files &&
	git -C bare sparse-checkout check-rules --cone \
		--rules-file ../rules >check-rules-file <all-files &&

	git -C repo sparse-checkout set --cone --stdin <rules&&
	git -C repo ls-files -t >out &&
	sed -n "/^S /!s/^. //p" out >ls-files &&

	git -C repo sparse-checkout check-rules >check-rules-default <all-files &&

	test_i18ngrep "deep/deeper1/deepest/a" check-rules-file &&
	test_i18ngrep ! "deep/deeper2" check-rules-file &&

	test_cmp check-rules-file ls-files &&
	test_cmp check-rules-file check-rules-default
'

test_expect_success 'check-rules non-cone mode' '
	cat >rules <<-\EOF &&
	deep/deeper1/deepest/a
	EOF

	git -C bare ls-tree -r --name-only HEAD >all-files &&
	git -C bare sparse-checkout check-rules --no-cone --rules-file ../rules\
		>check-rules-file <all-files &&

	cat rules | git -C repo sparse-checkout set --no-cone --stdin &&
	git -C repo ls-files -t >out &&
	sed -n "/^S /!s/^. //p" out >ls-files &&

	git -C repo sparse-checkout check-rules >check-rules-default <all-files &&

	cat >expect <<-\EOF &&
	deep/deeper1/deepest/a
	EOF

	test_cmp expect check-rules-file &&
	test_cmp check-rules-file ls-files &&
	test_cmp check-rules-file check-rules-default
'

test_expect_success 'check-rules cone mode is default' '
	cat >rules <<-\EOF &&
	folder1
	EOF

	cat >all-files <<-\EOF &&
	toplevel
	folder2/file
	folder1/file
	EOF

	cat >expect <<-\EOF &&
	toplevel
	folder1/file
	EOF

	git -C repo sparse-checkout set --no-cone &&
	git -C repo sparse-checkout check-rules \
		--rules-file ../rules >actual <all-files &&

	git -C bare sparse-checkout check-rules \
		--rules-file ../rules >actual-bare <all-files &&

	test_cmp expect actual &&
	test_cmp expect actual-bare
'

test_expect_success 'check-rules quoting' '
	cat >rules <<-EOF &&
	"folder\" a"
	EOF
	cat >files <<-EOF &&
	"folder\" a/file"
	"folder\" b/file"
	EOF
	cat >expect <<-EOF &&
	"folder\" a/file"
	EOF
	git sparse-checkout check-rules --cone \
		--rules-file rules >actual <files &&

	test_cmp expect actual
'

test_expect_success 'check-rules null termination' '
	cat >rules <<-EOF &&
	"folder\" a"
	EOF

	lf_to_nul >files <<-EOF &&
	folder" a/a
	folder" a/b
	folder" b/fileQ
	EOF

	cat >expect <<-EOF &&
	folder" a/aQfolder" a/bQ
	EOF

	git sparse-checkout check-rules --cone -z \
		--rules-file rules >actual.nul <files &&
	nul_to_q <actual.nul >actual &&
	echo >>actual &&

	test_cmp expect actual
'


test_done
