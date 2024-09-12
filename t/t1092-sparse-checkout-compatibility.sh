#!/bin/sh

test_description='compare full workdir to sparse workdir'

GIT_TEST_SPLIT_INDEX=0
GIT_TEST_SPARSE_INDEX=

. ./test-lib.sh

test_expect_success 'setup' '
	git init initial-repo &&
	(
		GIT_TEST_SPARSE_INDEX=0 &&
		cd initial-repo &&
		echo a >a &&
		echo "after deep" >e &&
		echo "after folder1" >g &&
		echo "after x" >z &&
		mkdir folder1 folder2 deep before x &&
		echo "before deep" >before/a &&
		echo "before deep again" >before/b &&
		mkdir deep/deeper1 deep/deeper2 deep/before deep/later &&
		mkdir deep/deeper1/deepest &&
		mkdir deep/deeper1/deepest2 &&
		mkdir deep/deeper1/deepest3 &&
		echo "after deeper1" >deep/e &&
		echo "after deepest" >deep/deeper1/e &&
		cp a folder1 &&
		cp a folder2 &&
		cp a x &&
		cp a deep &&
		cp a deep/before &&
		cp a deep/deeper1 &&
		cp a deep/deeper2 &&
		cp a deep/later &&
		cp a deep/deeper1/deepest &&
		cp a deep/deeper1/deepest2 &&
		cp a deep/deeper1/deepest3 &&
		cp -r deep/deeper1/ deep/deeper2 &&
		mkdir deep/deeper1/0 &&
		mkdir deep/deeper1/0/0 &&
		touch deep/deeper1/0/1 &&
		touch deep/deeper1/0/0/0 &&
		>folder1- &&
		>folder1.x &&
		>folder10 &&
		cp -r deep/deeper1/0 folder1 &&
		cp -r deep/deeper1/0 folder2 &&
		echo >>folder1/0/0/0 &&
		echo >>folder2/0/1 &&
		git add . &&
		git commit -m "initial commit" &&
		git checkout -b base &&
		for dir in folder1 folder2 deep
		do
			git checkout -b update-$dir base &&
			echo "updated $dir" >$dir/a &&
			git commit -a -m "update $dir" || return 1
		done &&

		git checkout -b rename-base base &&
		cat >folder1/larger-content <<-\EOF &&
		matching
		lines
		help
		inexact
		renames
		EOF
		cp folder1/larger-content folder2/ &&
		cp folder1/larger-content deep/deeper1/ &&
		git add . &&
		git commit -m "add interesting rename content" &&

		git checkout -b rename-out-to-out rename-base &&
		mv folder1/a folder2/b &&
		mv folder1/larger-content folder2/edited-content &&
		echo >>folder2/edited-content &&
		echo >>folder2/0/1 &&
		echo stuff >>deep/deeper1/a &&
		git add . &&
		git commit -m "rename folder1/... to folder2/..." &&

		git checkout -b rename-out-to-in rename-base &&
		mv folder1/a deep/deeper1/b &&
		echo more stuff >>deep/deeper1/a &&
		rm folder2/0/1 &&
		mkdir folder2/0/1 &&
		echo >>folder2/0/1/1 &&
		mv folder1/larger-content deep/deeper1/edited-content &&
		echo >>deep/deeper1/edited-content &&
		git add . &&
		git commit -m "rename folder1/... to deep/deeper1/..." &&

		git checkout -b rename-in-to-out rename-base &&
		mv deep/deeper1/a folder1/b &&
		echo >>folder2/0/1 &&
		rm -rf folder1/0/0 &&
		echo >>folder1/0/0 &&
		mv deep/deeper1/larger-content folder1/edited-content &&
		echo >>folder1/edited-content &&
		git add . &&
		git commit -m "rename deep/deeper1/... to folder1/..." &&

		git checkout -b df-conflict-1 base &&
		rm -rf folder1 &&
		echo content >folder1 &&
		git add . &&
		git commit -m "dir to file" &&

		git checkout -b df-conflict-2 base &&
		rm -rf folder2 &&
		echo content >folder2 &&
		git add . &&
		git commit -m "dir to file" &&

		git checkout -b fd-conflict base &&
		rm a &&
		mkdir a &&
		echo content >a/a &&
		git add . &&
		git commit -m "file to dir" &&

		for side in left right
		do
			git checkout -b merge-$side base &&
			echo $side >>deep/deeper2/a &&
			echo $side >>folder1/a &&
			echo $side >>folder2/a &&
			git add . &&
			git commit -m "$side" || return 1
		done &&

		git checkout -b deepest base &&
		echo "updated deepest" >deep/deeper1/deepest/a &&
		echo "updated deepest2" >deep/deeper1/deepest2/a &&
		echo "updated deepest3" >deep/deeper1/deepest3/a &&
		git commit -a -m "update deepest" &&

		git checkout -f base &&
		git reset --hard
	)
'

init_repos () {
	rm -rf full-checkout sparse-checkout sparse-index &&

	# create repos in initial state
	cp -r initial-repo full-checkout &&
	git -C full-checkout reset --hard &&

	cp -r initial-repo sparse-checkout &&
	git -C sparse-checkout reset --hard &&

	cp -r initial-repo sparse-index &&
	git -C sparse-index reset --hard &&

	# initialize sparse-checkout definitions
	git -C sparse-checkout sparse-checkout init --cone &&
	git -C sparse-checkout sparse-checkout set deep &&
	git -C sparse-index sparse-checkout init --cone --sparse-index &&
	test_cmp_config -C sparse-index true index.sparse &&
	git -C sparse-index sparse-checkout set deep &&

	# Disable this message to keep stderr the same.
	git -C sparse-index config advice.sparseIndexExpanded false
}

init_repos_as_submodules () {
	git reset --hard &&
	init_repos &&
	git submodule add ./full-checkout &&
	git submodule add ./sparse-checkout &&
	git submodule add ./sparse-index &&

	git submodule status >actual &&
	grep full-checkout actual &&
	grep sparse-checkout actual &&
	grep sparse-index actual
}

run_on_sparse () {
	cat >run-on-sparse-input &&

	(
		cd sparse-checkout &&
		GIT_PROGRESS_DELAY=100000 "$@" >../sparse-checkout-out 2>../sparse-checkout-err
	) <run-on-sparse-input &&
	(
		cd sparse-index &&
		GIT_PROGRESS_DELAY=100000 "$@" >../sparse-index-out 2>../sparse-index-err
	) <run-on-sparse-input
}

run_on_all () {
	cat >run-on-all-input &&

	(
		cd full-checkout &&
		GIT_PROGRESS_DELAY=100000 "$@" >../full-checkout-out 2>../full-checkout-err
	) <run-on-all-input &&
	run_on_sparse "$@" <run-on-all-input
}

test_all_match () {
	run_on_all "$@" &&
	test_cmp full-checkout-out sparse-checkout-out &&
	test_cmp full-checkout-out sparse-index-out &&
	test_cmp full-checkout-err sparse-checkout-err &&
	test_cmp full-checkout-err sparse-index-err
}

test_sparse_match () {
	run_on_sparse "$@" &&
	test_cmp sparse-checkout-out sparse-index-out &&
	test_cmp sparse-checkout-err sparse-index-err
}

test_sparse_unstaged () {
	file=$1 &&
	for repo in sparse-checkout sparse-index
	do
		# Skip "unmerged" paths
		git -C $repo diff --staged --diff-filter=u -- "$file" >diff &&
		test_must_be_empty diff || return 1
	done
}

# Usage: test_sparse_checkout_set "<c1> ... <cN>" "<s1> ... <sM>"
# Verifies that "git sparse-checkout set <c1> ... <cN>" succeeds and
# leaves the sparse index in a state where <s1> ... <sM> are sparse
# directories (and <c1> ... <cN> are not).
test_sparse_checkout_set () {
	CONE_DIRS=$1 &&
	SPARSE_DIRS=$2 &&
	git -C sparse-index sparse-checkout set --skip-checks $CONE_DIRS &&
	git -C sparse-index ls-files --sparse --stage >cache &&

	# Check that the directories outside of the sparse-checkout cone
	# have sparse directory entries.
	for dir in $SPARSE_DIRS
	do
		TREE=$(git -C sparse-index rev-parse HEAD:$dir) &&
		grep "040000 $TREE 0	$dir/" cache \
			|| return 1
	done &&

	# Check that the directories in the sparse-checkout cone
	# are not sparse directory entries.
	for dir in $CONE_DIRS
	do
		# Allow TREE to not exist because
		# $dir does not exist at HEAD.
		TREE=$(git -C sparse-index rev-parse HEAD:$dir) ||
		! grep "040000 $TREE 0	$dir/" cache \
			|| return 1
	done
}

test_expect_success 'sparse-index contents' '
	init_repos &&

	# Remove deep, add three other directories.
	test_sparse_checkout_set \
		"folder1 folder2 x" \
		"before deep" &&

	# Remove folder1, add deep
	test_sparse_checkout_set \
		"deep folder2 x" \
		"before folder1" &&

	# Replace deep with deep/deeper2 (dropping deep/deeper1)
	# Add folder1
	test_sparse_checkout_set \
		"deep/deeper2 folder1 folder2 x" \
		"before deep/deeper1" &&

	# Replace deep/deeper2 with deep/deeper1
	# Replace folder1 with folder1/0/0
	# Replace folder2 with non-existent folder2/2/3
	# Add non-existent "bogus"
	test_sparse_checkout_set \
		"bogus deep/deeper1 folder1/0/0 folder2/2/3 x" \
		"before deep/deeper2 folder2/0" &&

	# Drop down to only files at root
	test_sparse_checkout_set \
		"" \
		"before deep folder1 folder2 x" &&

	# Disabling the sparse-index replaces tree entries with full ones
	git -C sparse-index sparse-checkout init --no-sparse-index &&
	test_sparse_match git ls-files --stage --sparse
'

test_expect_success 'expanded in-memory index matches full index' '
	init_repos &&
	test_sparse_match git ls-files --stage
'

test_expect_success 'root directory cannot be sparse' '
	init_repos &&

	# Remove all in-cone files and directories from the index, collapse index
	# with `git sparse-checkout reapply`
	git -C sparse-index rm -r . &&
	git -C sparse-index sparse-checkout reapply &&

	# Verify sparse directories still present, root directory is not sparse
	cat >expect <<-EOF &&
	before/
	folder1/
	folder2/
	x/
	EOF
	git -C sparse-index ls-files --sparse >actual &&
	test_cmp expect actual
'

test_expect_success 'status with options' '
	init_repos &&
	test_sparse_match ls &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git status --porcelain=v2 -z -u &&
	test_all_match git status --porcelain=v2 -uno &&
	run_on_all touch README.md &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git status --porcelain=v2 -z -u &&
	test_all_match git status --porcelain=v2 -uno &&
	test_all_match git add README.md &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git status --porcelain=v2 -z -u &&
	test_all_match git status --porcelain=v2 -uno
'

test_expect_success 'status with diff in unexpanded sparse directory' '
	init_repos &&
	test_all_match git checkout rename-base &&
	test_all_match git reset --soft rename-out-to-out &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'status reports sparse-checkout' '
	init_repos &&
	git -C sparse-checkout status >full &&
	git -C sparse-index status >sparse &&
	test_grep "You are in a sparse checkout with " full &&
	test_grep "You are in a sparse checkout." sparse
'

test_expect_success 'add, commit, checkout' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF
	run_on_all ../edit-contents README.md &&

	test_all_match git add README.md &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git commit -m "Add README.md" &&

	test_all_match git checkout HEAD~1 &&
	test_all_match git checkout - &&

	run_on_all ../edit-contents README.md &&

	test_all_match git add -A &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git commit -m "Extend README.md" &&

	test_all_match git checkout HEAD~1 &&
	test_all_match git checkout - &&

	run_on_all ../edit-contents deep/newfile &&

	test_all_match git status --porcelain=v2 -uno &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git add . &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git commit -m "add deep/newfile" &&

	test_all_match git checkout HEAD~1 &&
	test_all_match git checkout -
'

test_expect_success 'deep changes during checkout' '
	init_repos &&

	test_sparse_match git sparse-checkout set deep/deeper1/deepest &&
	test_all_match git checkout deepest &&
	test_all_match git checkout base
'

test_expect_success 'checkout with modified sparse directory' '
	init_repos &&

	test_all_match git checkout rename-in-to-out -- . &&
	test_sparse_match git sparse-checkout reapply &&
	test_all_match git checkout base
'

test_expect_success 'checkout orphan then non-orphan' '
	init_repos &&

	test_all_match git checkout --orphan test-orphan &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git checkout base &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'add outside sparse cone' '
	init_repos &&

	run_on_sparse mkdir folder1 &&
	run_on_sparse ../edit-contents folder1/a &&
	run_on_sparse ../edit-contents folder1/newfile &&
	test_sparse_match test_must_fail git add folder1/a &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder1/a &&
	test_sparse_match test_must_fail git add folder1/newfile &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder1/newfile
'

test_expect_success 'commit including unstaged changes' '
	init_repos &&

	write_script edit-file <<-\EOF &&
	echo $1 >$2
	EOF

	run_on_all ../edit-file 1 a &&
	run_on_all ../edit-file 1 deep/a &&

	test_all_match git commit -m "-a" -a &&
	test_all_match git status --porcelain=v2 &&

	run_on_all ../edit-file 2 a &&
	run_on_all ../edit-file 2 deep/a &&

	test_all_match git commit -m "--include" --include deep/a &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git commit -m "--include" --include a &&
	test_all_match git status --porcelain=v2 &&

	run_on_all ../edit-file 3 a &&
	run_on_all ../edit-file 3 deep/a &&

	test_all_match git commit -m "--amend" -a --amend &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'status/add: outside sparse cone' '
	init_repos &&

	# folder1 is at HEAD, but outside the sparse cone
	run_on_sparse mkdir folder1 &&
	cp initial-repo/folder1/a sparse-checkout/folder1/a &&
	cp initial-repo/folder1/a sparse-index/folder1/a &&

	test_sparse_match git status &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF
	run_on_all ../edit-contents folder1/a &&
	run_on_all ../edit-contents folder1/new &&

	test_sparse_match git status --porcelain=v2 &&

	# Adding the path outside of the sparse-checkout cone should fail.
	test_sparse_match test_must_fail git add folder1/a &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder1/a &&
	test_all_match git add --refresh folder1/a &&
	test_must_be_empty sparse-checkout-err &&
	test_sparse_unstaged folder1/a &&
	test_sparse_match test_must_fail git add folder1/new &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder1/new &&
	test_sparse_match git add --sparse folder1/a &&
	test_sparse_match git add --sparse folder1/new &&

	test_all_match git add --sparse . &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git commit -m folder1/new &&
	test_all_match git rev-parse HEAD^{tree} &&

	run_on_all ../edit-contents folder1/newer &&
	test_all_match git add --sparse folder1/ &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git commit -m folder1/newer &&
	test_all_match git rev-parse HEAD^{tree}
'

test_expect_success 'checkout and reset --hard' '
	init_repos &&

	test_all_match git checkout update-folder1 &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git checkout update-deep &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git checkout -b reset-test &&
	test_all_match git reset --hard deepest &&
	test_all_match git reset --hard update-folder1 &&
	test_all_match git reset --hard update-folder2
'

test_expect_success 'diff --cached' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>README.md
	EOF
	run_on_all ../edit-contents &&

	test_all_match git diff &&
	test_all_match git diff --cached &&
	test_all_match git add README.md &&
	test_all_match git diff &&
	test_all_match git diff --cached
'

# NEEDSWORK: sparse-checkout behaves differently from full-checkout when
# running this test with 'df-conflict-2' after 'df-conflict-1'.
test_expect_success 'diff with renames and conflicts' '
	init_repos &&

	for branch in rename-out-to-out \
		      rename-out-to-in \
		      rename-in-to-out \
		      df-conflict-1 \
		      fd-conflict
	do
		test_all_match git checkout rename-base &&
		test_all_match git checkout $branch -- . &&
		test_all_match git status --porcelain=v2 &&
		test_all_match git diff --cached --no-renames &&
		test_all_match git diff --cached --find-renames || return 1
	done
'

test_expect_success 'diff with directory/file conflicts' '
	init_repos &&

	for branch in rename-out-to-out \
		      rename-out-to-in \
		      rename-in-to-out \
		      df-conflict-1 \
		      df-conflict-2 \
		      fd-conflict
	do
		git -C full-checkout reset --hard &&
		test_sparse_match git reset --hard &&
		test_all_match git checkout $branch &&
		test_all_match git checkout rename-base -- . &&
		test_all_match git status --porcelain=v2 &&
		test_all_match git diff --cached --no-renames &&
		test_all_match git diff --cached --find-renames || return 1
	done
'

test_expect_success 'log with pathspec outside sparse definition' '
	init_repos &&

	test_all_match git log -- a &&
	test_all_match git log -- folder1/a &&
	test_all_match git log -- folder2/a &&
	test_all_match git log -- deep/a &&
	test_all_match git log -- deep/deeper1/a &&
	test_all_match git log -- deep/deeper1/deepest/a &&

	test_all_match git checkout update-folder1 &&
	test_all_match git log -- folder1/a
'

test_expect_success 'blame with pathspec inside sparse definition' '
	init_repos &&

	for file in a \
			deep/a \
			deep/deeper1/a \
			deep/deeper1/deepest/a
	do
		test_all_match git blame $file || return 1
	done
'

# Without a revision specified, blame will error if passed any file that
# is not present in the working directory (even if the file is tracked).
# Here we just verify that this is also true with sparse checkouts.
test_expect_success 'blame with pathspec outside sparse definition' '
	init_repos &&
	test_sparse_match git sparse-checkout set &&

	for file in \
			deep/a \
			deep/deeper1/a \
			deep/deeper1/deepest/a
	do
		test_sparse_match test_must_fail git blame $file &&
		cat >expect <<-EOF &&
		fatal: Cannot lstat '"'"'$file'"'"': No such file or directory
		EOF
		# We compare sparse-checkout-err and sparse-index-err in
		# `test_sparse_match`. Given we know they are the same, we
		# only check the content of sparse-index-err here.
		test_cmp expect sparse-index-err || return 1
	done
'

test_expect_success 'checkout and reset (mixed)' '
	init_repos &&

	test_all_match git checkout -b reset-test update-deep &&
	test_all_match git reset deepest &&

	# Because skip-worktree is preserved, resetting to update-folder1
	# will show worktree changes for folder1/a in full-checkout, but not
	# in sparse-checkout or sparse-index.
	git -C full-checkout reset update-folder1 >full-checkout-out &&
	test_sparse_match git reset update-folder1 &&
	grep "M	folder1/a" full-checkout-out &&
	! grep "M	folder1/a" sparse-checkout-out &&
	run_on_sparse test_path_is_missing folder1
'

test_expect_success 'checkout and reset (merge)' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	test_all_match git checkout -b reset-test update-deep &&
	run_on_all ../edit-contents a &&
	test_all_match git reset --merge deepest &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset --hard update-deep &&
	run_on_all ../edit-contents deep/a &&
	test_all_match test_must_fail git reset --merge deepest
'

test_expect_success 'checkout and reset (keep)' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	test_all_match git checkout -b reset-test update-deep &&
	run_on_all ../edit-contents a &&
	test_all_match git reset --keep deepest &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset --hard update-deep &&
	run_on_all ../edit-contents deep/a &&
	test_all_match test_must_fail git reset --keep deepest
'

test_expect_success 'reset with pathspecs inside sparse definition' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	test_all_match git checkout -b reset-test update-deep &&
	run_on_all ../edit-contents deep/a &&

	test_all_match git reset base -- deep/a &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset base -- nonexistent-file &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset deepest -- deep &&
	test_all_match git status --porcelain=v2
'

# Although the working tree differs between full and sparse checkouts after
# reset, the state of the index is the same.
test_expect_success 'reset with pathspecs outside sparse definition' '
	init_repos &&
	test_all_match git checkout -b reset-test base &&

	test_sparse_match git reset update-folder1 -- folder1 &&
	git -C full-checkout reset update-folder1 -- folder1 &&
	test_all_match git ls-files -s -- folder1 &&

	test_sparse_match git reset update-folder2 -- folder2/a &&
	git -C full-checkout reset update-folder2 -- folder2/a &&
	test_all_match git ls-files -s -- folder2/a
'

test_expect_success 'reset with wildcard pathspec' '
	init_repos &&

	test_all_match git reset update-deep -- deep\* &&
	test_all_match git ls-files -s -- deep &&

	test_all_match git reset deepest -- deep\*\*\* &&
	test_all_match git ls-files -s -- deep &&

	# The following `git reset`s result in updating the index on files with
	# `skip-worktree` enabled. To avoid failing due to discrepencies in reported
	# "modified" files, `test_sparse_match` reset is performed separately from
	# "full-checkout" reset, then the index contents of all repos are verified.

	test_sparse_match git reset update-folder1 -- \*/a &&
	git -C full-checkout reset update-folder1 -- \*/a &&
	test_all_match git ls-files -s -- deep/a folder1/a &&

	test_sparse_match git reset update-folder2 -- folder\* &&
	git -C full-checkout reset update-folder2 -- folder\* &&
	test_all_match git ls-files -s -- folder10 folder1 folder2 &&

	test_sparse_match git reset base -- folder1/\* &&
	git -C full-checkout reset base -- folder1/\* &&
	test_all_match git ls-files -s -- folder1
'

test_expect_success 'reset hard with removed sparse dir' '
	init_repos &&

	run_on_all git rm -r --sparse folder1 &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset --hard &&
	test_all_match git status --porcelain=v2 &&

	cat >expect <<-\EOF &&
	folder1/
	EOF

	git -C sparse-index ls-files --sparse folder1 >out &&
	test_cmp expect out
'

test_expect_success 'update-index modify outside sparse definition' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	# Create & modify folder1/a
	# Note that this setup is a manual way of reaching the erroneous
	# condition in which a `skip-worktree` enabled, outside-of-cone file
	# exists on disk. It is used here to ensure `update-index` is stable
	# and behaves predictably if such a condition occurs.
	run_on_sparse mkdir -p folder1 &&
	run_on_sparse cp ../initial-repo/folder1/a folder1/a &&
	run_on_all ../edit-contents folder1/a &&

	# If file has skip-worktree enabled, but the file is present, it is
	# treated the same as if skip-worktree is disabled
	test_all_match git status --porcelain=v2 &&
	test_all_match git update-index folder1/a &&
	test_all_match git status --porcelain=v2 &&

	# When skip-worktree is disabled (even on files outside sparse cone), file
	# is updated in the index
	test_sparse_match git update-index --no-skip-worktree folder1/a &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git update-index folder1/a &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'update-index --add outside sparse definition' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	# Create folder1, add new file
	run_on_sparse mkdir -p folder1 &&
	run_on_all ../edit-contents folder1/b &&

	# The *untracked* out-of-cone file is added to the index because it does
	# not have a `skip-worktree` bit to signal that it should be ignored
	# (unlike in `git add`, which will fail due to the file being outside
	# the sparse checkout definition).
	test_all_match git update-index --add folder1/b &&
	test_all_match git status --porcelain=v2
'

# NEEDSWORK: `--remove`, unlike the rest of `update-index`, does not ignore
# `skip-worktree` entries by default and will remove them from the index.
# The `--ignore-skip-worktree-entries` flag must be used in conjunction with
# `--remove` to ignore the `skip-worktree` entries and prevent their removal
# from the index.
test_expect_success 'update-index --remove outside sparse definition' '
	init_repos &&

	# When --ignore-skip-worktree-entries is _not_ specified:
	# out-of-cone, not-on-disk files are removed from the index
	test_sparse_match git update-index --remove folder1/a &&
	cat >expect <<-EOF &&
	D	folder1/a
	EOF
	test_sparse_match git diff --cached --name-status &&
	test_cmp expect sparse-checkout-out &&

	test_sparse_match git diff-index --cached HEAD &&

	# Reset the state
	test_all_match git reset --hard &&

	# When --ignore-skip-worktree-entries is specified, out-of-cone
	# (skip-worktree) files are ignored
	test_sparse_match git update-index --remove --ignore-skip-worktree-entries folder1/a &&
	test_sparse_match git diff --cached --name-status &&
	test_must_be_empty sparse-checkout-out &&

	test_sparse_match git diff-index --cached HEAD &&

	# Reset the state
	test_all_match git reset --hard &&

	# --force-remove supercedes --ignore-skip-worktree-entries, removing
	# a skip-worktree file from the index (and disk) when both are specified
	# with --remove
	test_sparse_match git update-index --force-remove --ignore-skip-worktree-entries folder1/a &&
	cat >expect <<-EOF &&
	D	folder1/a
	EOF
	test_sparse_match git diff --cached --name-status &&
	test_cmp expect sparse-checkout-out &&

	test_sparse_match git diff-index --cached HEAD
'

test_expect_success 'update-index with directories' '
	init_repos &&

	# update-index will exit silently when provided with a directory name
	# containing a trailing slash
	test_all_match git update-index deep/ folder1/ &&
	grep "Ignoring path deep/" sparse-checkout-err &&
	grep "Ignoring path folder1/" sparse-checkout-err &&

	# When update-index is given a directory name WITHOUT a trailing slash, it will
	# behave in different ways depending on the status of the directory on disk:
	# * if it exists, the command exits with an error ("add individual files instead")
	# * if it does NOT exist (e.g., in a sparse-checkout), it is assumed to be a
	#   file and either triggers an error ("does not exist  and --remove not passed")
	#   or is ignored completely (when using --remove)
	test_all_match test_must_fail git update-index deep &&
	run_on_all test_must_fail git update-index folder1 &&
	test_must_fail git -C full-checkout update-index --remove folder1 &&
	test_sparse_match git update-index --remove folder1 &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'update-index --again file outside sparse definition' '
	init_repos &&

	test_all_match git checkout -b test-reupdate &&

	# Update HEAD without modifying the index to introduce a difference in
	# folder1/a
	test_sparse_match git reset --soft update-folder1 &&

	# Because folder1/a differs in the index vs HEAD,
	# `git update-index --no-skip-worktree --again` will effectively perform
	# `git update-index --no-skip-worktree folder1/a` and remove the skip-worktree
	# flag from folder1/a
	test_sparse_match git update-index --no-skip-worktree --again &&
	test_sparse_match git status --porcelain=v2 &&

	cat >expect <<-EOF &&
	D	folder1/a
	EOF
	test_sparse_match git diff --name-status &&
	test_cmp expect sparse-checkout-out
'

test_expect_success 'update-index --cacheinfo' '
	init_repos &&

	deep_a_oid=$(git -C full-checkout rev-parse update-deep:deep/a) &&
	folder2_oid=$(git -C full-checkout rev-parse update-folder2:folder2) &&
	folder1_a_oid=$(git -C full-checkout rev-parse update-folder1:folder1/a) &&

	test_all_match git update-index --cacheinfo 100644 $deep_a_oid deep/a &&
	test_all_match git status --porcelain=v2 &&

	# Cannot add sparse directory, even in sparse index case
	test_all_match test_must_fail git update-index --add --cacheinfo 040000 $folder2_oid folder2/ &&

	# Sparse match only: the new outside-of-cone entry is added *without* skip-worktree,
	# so `git status` reports it as "deleted" in the worktree
	test_sparse_match git update-index --add --cacheinfo 100644 $folder1_a_oid folder1/a &&
	test_sparse_match git status --porcelain=v2 &&
	cat >expect <<-EOF &&
	MD folder1/a
	EOF
	test_sparse_match git status --short -- folder1/a &&
	test_cmp expect sparse-checkout-out &&

	# To return folder1/a to "normal" for a sparse checkout (ignored &
	# outside-of-cone), add the skip-worktree flag.
	test_sparse_match git update-index --skip-worktree folder1/a &&
	cat >expect <<-EOF &&
	S folder1/a
	EOF
	test_sparse_match git ls-files -t -- folder1/a &&
	test_cmp expect sparse-checkout-out
'

for MERGE_TREES in "base HEAD update-folder2" \
		   "update-folder1 update-folder2" \
		   "update-folder2"
do
	test_expect_success "'read-tree -mu $MERGE_TREES' with files outside sparse definition" '
		init_repos &&

		# Although the index matches, without --no-sparse-checkout, outside-of-
		# definition files will not exist on disk for sparse checkouts
		test_all_match git read-tree -mu $MERGE_TREES &&
		test_all_match git status --porcelain=v2 &&
		test_path_is_missing sparse-checkout/folder2 &&
		test_path_is_missing sparse-index/folder2 &&

		test_all_match git read-tree --reset -u HEAD &&
		test_all_match git status --porcelain=v2 &&

		test_all_match git read-tree -mu --no-sparse-checkout $MERGE_TREES &&
		test_all_match git status --porcelain=v2 &&
		test_cmp sparse-checkout/folder2/a sparse-index/folder2/a &&
		test_cmp sparse-checkout/folder2/a full-checkout/folder2/a

	'
done

test_expect_success 'read-tree --merge with edit/edit conflicts in sparse directories' '
	init_repos &&

	# Merge of multiple changes to same directory (but not same files) should
	# succeed
	test_all_match git read-tree -mu base rename-base update-folder1 &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset --hard &&

	test_all_match git read-tree -mu rename-base update-folder2 &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git reset --hard &&

	test_all_match test_must_fail git read-tree -mu base update-folder1 rename-out-to-in &&
	test_all_match test_must_fail git read-tree -mu rename-out-to-in update-folder1
'

test_expect_success 'read-tree --prefix' '
	init_repos &&

	# If files differing between the index and target <commit-ish> exist
	# inside the prefix, `read-tree --prefix` should fail
	test_all_match test_must_fail git read-tree --prefix=deep/ deepest &&
	test_all_match test_must_fail git read-tree --prefix=folder1/ update-folder1 &&

	# If no differing index entries exist matching the prefix,
	# `read-tree --prefix` updates the index successfully
	test_all_match git rm -rf deep/deeper1/deepest/ &&
	test_all_match git read-tree --prefix=deep/deeper1/deepest -u deepest &&
	test_all_match git status --porcelain=v2 &&

	run_on_all git rm -rf --sparse folder1/ &&
	test_all_match git read-tree --prefix=folder1/ -u update-folder1 &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git rm -rf --sparse folder2/0 &&
	test_all_match git read-tree --prefix=folder2/0/ -u rename-out-to-out &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'read-tree --merge with directory-file conflicts' '
	init_repos &&

	test_all_match git checkout -b test-branch rename-base &&

	# Although the index matches, without --no-sparse-checkout, outside-of-
	# definition files will not exist on disk for sparse checkouts
	test_sparse_match git read-tree -mu rename-out-to-out &&
	test_sparse_match git status --porcelain=v2 &&
	test_path_is_missing sparse-checkout/folder2 &&
	test_path_is_missing sparse-index/folder2 &&

	test_sparse_match git read-tree --reset -u HEAD &&
	test_sparse_match git status --porcelain=v2 &&

	test_sparse_match git read-tree -mu --no-sparse-checkout rename-out-to-out &&
	test_sparse_match git status --porcelain=v2 &&
	test_cmp sparse-checkout/folder2/0/1 sparse-index/folder2/0/1
'

test_expect_success 'merge, cherry-pick, and rebase' '
	init_repos &&

	for OPERATION in "merge -m merge" cherry-pick "rebase --apply" "rebase --merge"
	do
		test_all_match git checkout -B temp update-deep &&
		test_all_match git $OPERATION update-folder1 &&
		test_all_match git rev-parse HEAD^{tree} &&
		test_all_match git $OPERATION update-folder2 &&
		test_all_match git rev-parse HEAD^{tree} || return 1
	done
'

test_expect_success 'merge with conflict outside cone' '
	init_repos &&

	test_all_match git checkout -b merge-tip merge-left &&
	test_all_match git status --porcelain=v2 &&
	test_all_match test_must_fail git merge -m merge merge-right &&
	test_all_match git status --porcelain=v2 &&

	# Resolve the conflict in different ways:
	# 1. Revert to the base
	test_all_match git checkout base -- deep/deeper2/a &&
	test_all_match git status --porcelain=v2 &&

	# 2. Add the file with conflict markers
	test_sparse_match test_must_fail git add folder1/a &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder1/a &&
	test_all_match git add --sparse folder1/a &&
	test_all_match git status --porcelain=v2 &&

	# 3. Rename the file to another sparse filename and
	#    accept conflict markers as resolved content.
	run_on_all mv folder2/a folder2/z &&
	test_sparse_match test_must_fail git add folder2 &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder2/z &&
	test_all_match git add --sparse folder2 &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git merge --continue &&
	test_all_match git status --porcelain=v2 &&
	test_all_match git rev-parse HEAD^{tree}
'

test_expect_success 'cherry-pick/rebase with conflict outside cone' '
	init_repos &&

	for OPERATION in cherry-pick rebase
	do
		test_all_match git checkout -B tip &&
		test_all_match git reset --hard merge-left &&
		test_all_match git status --porcelain=v2 &&
		test_all_match test_must_fail git $OPERATION merge-right &&
		test_all_match git status --porcelain=v2 &&

		# Resolve the conflict in different ways:
		# 1. Revert to the base
		test_all_match git checkout base -- deep/deeper2/a &&
		test_all_match git status --porcelain=v2 &&

		# 2. Add the file with conflict markers
		# NEEDSWORK: Even though the merge conflict removed the
		# SKIP_WORKTREE bit from the index entry for folder1/a, we should
		# warn that this is a problematic add.
		test_sparse_match test_must_fail git add folder1/a &&
		grep "Disable or modify the sparsity rules" sparse-checkout-err &&
		test_sparse_unstaged folder1/a &&
		test_all_match git add --sparse folder1/a &&
		test_all_match git status --porcelain=v2 &&

		# 3. Rename the file to another sparse filename and
		#    accept conflict markers as resolved content.
		# NEEDSWORK: This mode now fails, because folder2/z is
		# outside of the sparse-checkout cone and does not match an
		# existing index entry with the SKIP_WORKTREE bit cleared.
		run_on_all mv folder2/a folder2/z &&
		test_sparse_match test_must_fail git add folder2 &&
		grep "Disable or modify the sparsity rules" sparse-checkout-err &&
		test_sparse_unstaged folder2/z &&
		test_all_match git add --sparse folder2 &&
		test_all_match git status --porcelain=v2 &&

		test_all_match git $OPERATION --continue &&
		test_all_match git status --porcelain=v2 &&
		test_all_match git rev-parse HEAD^{tree} || return 1
	done
'

test_expect_success 'merge with outside renames' '
	init_repos &&

	for type in out-to-out out-to-in in-to-out
	do
		test_all_match git reset --hard &&
		test_all_match git checkout -f -b merge-$type update-deep &&
		test_all_match git merge -m "$type" rename-$type &&
		test_all_match git rev-parse HEAD^{tree} || return 1
	done
'

# Sparse-index fails to convert the index in the
# final 'git cherry-pick' command.
test_expect_success 'cherry-pick with conflicts' '
	init_repos &&

	write_script edit-conflict <<-\EOF &&
	echo $1 >conflict
	EOF

	test_all_match git checkout -b to-cherry-pick &&
	run_on_all ../edit-conflict ABC &&
	test_all_match git add conflict &&
	test_all_match git commit -m "conflict to pick" &&

	test_all_match git checkout -B base HEAD~1 &&
	run_on_all ../edit-conflict DEF &&
	test_all_match git add conflict &&
	test_all_match git commit -m "conflict in base" &&

	test_all_match test_must_fail git cherry-pick to-cherry-pick
'

test_expect_success 'stash' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	# Stash a sparse directory (folder1)
	test_all_match git checkout -b test-branch rename-base &&
	test_all_match git reset --soft rename-out-to-out &&
	test_all_match git stash &&
	test_all_match git status --porcelain=v2 &&

	# Apply the sparse directory stash without reinstating the index
	test_all_match git stash apply -q &&
	test_all_match git status --porcelain=v2 &&

	# Reset to state where stash can be applied
	test_sparse_match git sparse-checkout reapply &&
	test_all_match git reset --hard rename-out-to-out &&

	# Apply the sparse directory stash *with* reinstating the index
	test_all_match git stash apply --index -q &&
	test_all_match git status --porcelain=v2 &&

	# Reset to state where we will get a conflict applying the stash
	test_sparse_match git sparse-checkout reapply &&
	test_all_match git reset --hard update-folder1 &&

	# Apply the sparse directory stash with conflicts
	test_all_match test_must_fail git stash apply --index -q &&
	test_all_match test_must_fail git stash apply -q &&
	test_all_match git status --porcelain=v2 &&

	# Reset to base branch
	test_sparse_match git sparse-checkout reapply &&
	test_all_match git reset --hard base &&

	# Stash & unstash an untracked file outside of the sparse checkout
	# definition.
	run_on_sparse mkdir -p folder1 &&
	run_on_all ../edit-contents folder1/new &&
	test_all_match git stash -u &&
	test_all_match git status --porcelain=v2 &&

	test_all_match git stash pop -q &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'checkout-index inside sparse definition' '
	init_repos &&

	run_on_all rm -f deep/a &&
	test_all_match git checkout-index -- deep/a &&
	test_all_match git status --porcelain=v2 &&

	echo test >>new-a &&
	run_on_all cp ../new-a a &&
	test_all_match test_must_fail git checkout-index -- a &&
	test_all_match git checkout-index -f -- a &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'checkout-index outside sparse definition' '
	init_repos &&

	# Without --ignore-skip-worktree-bits, outside-of-cone files will trigger
	# an error
	test_sparse_match test_must_fail git checkout-index -- folder1/a &&
	test_grep "folder1/a has skip-worktree enabled" sparse-checkout-err &&
	test_path_is_missing folder1/a &&

	# With --ignore-skip-worktree-bits, outside-of-cone files are checked out
	test_sparse_match git checkout-index --ignore-skip-worktree-bits -- folder1/a &&
	test_cmp sparse-checkout/folder1/a sparse-index/folder1/a &&
	test_cmp sparse-checkout/folder1/a full-checkout/folder1/a &&

	run_on_sparse rm -rf folder1 &&
	echo test >new-a &&
	run_on_sparse mkdir -p folder1 &&
	run_on_all cp ../new-a folder1/a &&

	test_all_match test_must_fail git checkout-index --ignore-skip-worktree-bits -- folder1/a &&
	test_all_match git checkout-index -f --ignore-skip-worktree-bits -- folder1/a &&
	test_cmp sparse-checkout/folder1/a sparse-index/folder1/a &&
	test_cmp sparse-checkout/folder1/a full-checkout/folder1/a
'

test_expect_success 'checkout-index with folders' '
	init_repos &&

	# Inside checkout definition
	test_all_match test_must_fail git checkout-index -f -- deep/ &&

	# Outside checkout definition
	# Note: although all tests fail (as expected), the messaging differs. For
	# non-sparse index checkouts, the error is that the "file" does not appear
	# in the index; for sparse checkouts, the error is explicitly that the
	# entry is a sparse directory.
	run_on_all test_must_fail git checkout-index -f -- folder1/ &&
	test_cmp full-checkout-err sparse-checkout-err &&
	! test_cmp full-checkout-err sparse-index-err &&
	grep "is a sparse directory" sparse-index-err
'

test_expect_success 'checkout-index --all' '
	init_repos &&

	test_all_match git checkout-index --all &&
	test_sparse_match test_path_is_missing folder1 &&

	# --ignore-skip-worktree-bits will cause `skip-worktree` files to be
	# checked out, causing the outside-of-cone `folder1` to exist on-disk
	test_all_match git checkout-index --ignore-skip-worktree-bits --all &&
	test_all_match test_path_exists folder1
'

test_expect_success 'clean' '
	init_repos &&

	echo bogus >>.gitignore &&
	run_on_all cp ../.gitignore . &&
	test_all_match git add .gitignore &&
	test_all_match git commit -m "ignore bogus files" &&

	run_on_sparse mkdir folder1 &&
	run_on_all mkdir -p deep/untracked-deep &&
	run_on_all touch folder1/bogus &&
	run_on_all touch folder1/untracked &&
	run_on_all touch deep/untracked-deep/bogus &&
	run_on_all touch deep/untracked-deep/untracked &&

	test_all_match git status --porcelain=v2 &&
	test_all_match git clean -f &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&
	run_on_all test_path_exists folder1/bogus &&
	run_on_all test_path_is_missing folder1/untracked &&
	run_on_all test_path_exists deep/untracked-deep/bogus &&
	run_on_all test_path_exists deep/untracked-deep/untracked &&

	test_all_match git clean -fd &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&
	run_on_all test_path_exists folder1/bogus &&
	run_on_all test_path_exists deep/untracked-deep/bogus &&
	run_on_all test_path_is_missing deep/untracked-deep/untracked &&

	test_all_match git clean -xf &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&
	run_on_all test_path_is_missing folder1/bogus &&
	run_on_all test_path_exists deep/untracked-deep/bogus &&

	test_all_match git clean -xdf &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&
	run_on_all test_path_is_missing deep/untracked-deep/bogus &&

	test_sparse_match test_path_is_dir folder1
'

for builtin in show rev-parse
do
	test_expect_success "$builtin (cached blobs/trees)" "
		init_repos &&

		test_all_match git $builtin :a &&
		test_all_match git $builtin :deep/a &&
		test_sparse_match git $builtin :folder1/a &&

		# The error message differs depending on whether
		# the directory exists in the worktree.
		test_all_match test_must_fail git $builtin :deep/ &&
		test_must_fail git -C full-checkout $builtin :folder1/ &&
		test_sparse_match test_must_fail git $builtin :folder1/ &&

		# Change the sparse cone for an extra case:
		run_on_sparse git sparse-checkout set deep/deeper1 &&

		# deep/deeper2 is a sparse directory in the sparse index.
		test_sparse_match test_must_fail git $builtin :deep/deeper2/ &&

		# deep/deeper2/deepest is not in the sparse index, but
		# will trigger an index expansion.
		test_sparse_match test_must_fail git $builtin :deep/deeper2/deepest/
	"
done

test_expect_success 'submodule handling' '
	init_repos &&

	test_sparse_match git sparse-checkout add modules &&
	test_all_match mkdir modules &&
	test_all_match touch modules/a &&
	test_all_match git add modules &&
	test_all_match git commit -m "add modules directory" &&

	test_config_global protocol.file.allow always &&

	run_on_all git submodule add "$(pwd)/initial-repo" modules/sub &&
	test_all_match git commit -m "add submodule" &&

	# having a submodule prevents "modules" from collapse
	test_sparse_match git sparse-checkout set deep/deeper1 &&
	git -C sparse-index ls-files --sparse --stage >cache &&
	grep "100644 .*	modules/a" cache &&
	grep "160000 $(git -C initial-repo rev-parse HEAD) 0	modules/sub" cache
'

# When working with a sparse index, some commands will need to expand the
# index to operate properly. If those commands also write the index back
# to disk, they need to convert the index to sparse before writing.
# This test verifies that both of these events are logged in trace2 logs.
test_expect_success 'sparse-index is expanded and converted back' '
	init_repos &&

	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		git -C sparse-index reset -- folder1/a &&
	test_region index convert_to_sparse trace2.txt &&
	test_region index ensure_full_index trace2.txt &&

	# ls-files expands on read, but does not write.
	rm trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
		git -C sparse-index ls-files &&
	test_region index ensure_full_index trace2.txt
'

test_expect_success 'index.sparse disabled inline uses full index' '
	init_repos &&

	# When index.sparse is disabled inline with `git status`, the
	# index is expanded at the beginning of the execution then never
	# converted back to sparse. It is then written to disk as a full index.
	rm -f trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
		git -C sparse-index -c index.sparse=false status &&
	! test_region index convert_to_sparse trace2.txt &&
	test_region index ensure_full_index trace2.txt &&

	# Since index.sparse is set to true at a repo level, the index
	# is converted from full to sparse when read, then never expanded
	# over the course of `git status`. It is written to disk as a sparse
	# index.
	rm -f trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
		git -C sparse-index status &&
	test_region index convert_to_sparse trace2.txt &&
	! test_region index ensure_full_index trace2.txt &&

	# Now that the index has been written to disk as sparse, it is not
	# converted to sparse (or expanded to full) when read by `git status`.
	rm -f trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
		git -C sparse-index status &&
	! test_region index convert_to_sparse trace2.txt &&
	! test_region index ensure_full_index trace2.txt
'

run_sparse_index_trace2 () {
	rm -f trace2.txt &&
	if test -z "$WITHOUT_UNTRACKED_TXT"
	then
		echo >>sparse-index/untracked.txt
	fi &&

	if test "$1" = "!"
	then
		shift &&
		test_must_fail env \
			GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
			git -C sparse-index "$@" \
			>sparse-index-out \
			2>sparse-index-error || return 1
	else
		GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
			git -C sparse-index "$@" \
			>sparse-index-out \
			2>sparse-index-error || return 1
	fi
}

ensure_expanded () {
	run_sparse_index_trace2 "$@" &&
	test_region index ensure_full_index trace2.txt
}

ensure_not_expanded () {
	run_sparse_index_trace2 "$@" &&
	test_region ! index ensure_full_index trace2.txt
}

test_expect_success 'sparse-index is not expanded' '
	init_repos &&

	ensure_not_expanded status &&
	ensure_not_expanded ls-files --sparse &&
	ensure_not_expanded commit --allow-empty -m empty &&
	echo >>sparse-index/a &&
	ensure_not_expanded commit -a -m a &&
	echo >>sparse-index/a &&
	ensure_not_expanded commit --include a -m a &&
	echo >>sparse-index/deep/deeper1/a &&
	ensure_not_expanded commit --include deep/deeper1/a -m deeper &&
	ensure_not_expanded checkout rename-out-to-out &&
	ensure_not_expanded checkout - &&
	ensure_not_expanded switch rename-out-to-out &&
	ensure_not_expanded switch - &&
	ensure_not_expanded reset --hard &&
	ensure_not_expanded checkout rename-out-to-out -- deep/deeper1 &&
	ensure_not_expanded reset --hard &&
	ensure_not_expanded restore -s rename-out-to-out -- deep/deeper1 &&

	echo >>sparse-index/README.md &&
	ensure_not_expanded add -A &&
	echo >>sparse-index/extra.txt &&
	ensure_not_expanded add extra.txt &&
	echo >>sparse-index/untracked.txt &&
	ensure_not_expanded add . &&

	ensure_not_expanded checkout-index -f a &&
	ensure_not_expanded checkout-index -f --all &&
	for ref in update-deep update-folder1 update-folder2 update-deep
	do
		echo >>sparse-index/README.md &&
		ensure_not_expanded reset --hard $ref || return 1
	done &&

	ensure_not_expanded reset --mixed base &&
	ensure_not_expanded reset --hard update-deep &&
	ensure_not_expanded reset --keep base &&
	ensure_not_expanded reset --merge update-deep &&
	ensure_not_expanded reset --hard &&

	ensure_not_expanded reset base -- deep/a &&
	ensure_not_expanded reset base -- nonexistent-file &&
	ensure_not_expanded reset deepest -- deep &&

	# Although folder1 is outside the sparse definition, it exists as a
	# directory entry in the index, so the pathspec will not force the
	# index to be expanded.
	ensure_not_expanded reset deepest -- folder1 &&
	ensure_not_expanded reset deepest -- folder1/ &&

	# Wildcard identifies only in-cone files, no index expansion
	ensure_not_expanded reset deepest -- deep/\* &&

	# Wildcard identifies only full sparse directories, no index expansion
	ensure_not_expanded reset deepest -- folder\* &&

	ensure_not_expanded clean -fd &&

	ensure_not_expanded checkout -f update-deep &&
	test_config -C sparse-index pull.twohead ort &&
	(
		sane_unset GIT_TEST_MERGE_ALGORITHM &&
		for OPERATION in "merge -m merge" cherry-pick rebase
		do
			ensure_not_expanded merge -m merge update-folder1 &&
			ensure_not_expanded merge -m merge update-folder2 || return 1
		done
	)
'

test_expect_success 'sparse-index is not expanded: merge conflict in cone' '
	init_repos &&

	for side in right left
	do
		git -C sparse-index checkout -b expand-$side base &&
		echo $side >sparse-index/deep/a &&
		git -C sparse-index commit -a -m "$side" || return 1
	done &&

	(
		sane_unset GIT_TEST_MERGE_ALGORITHM &&
		git -C sparse-index config pull.twohead ort &&
		ensure_not_expanded ! merge -m merged expand-right
	)
'

test_expect_success 'sparse-index is not expanded: stash' '
	init_repos &&

	echo >>sparse-index/a &&
	ensure_not_expanded stash &&
	ensure_not_expanded stash list &&
	ensure_not_expanded stash show stash@{0} &&
	ensure_not_expanded stash apply stash@{0} &&
	ensure_not_expanded stash drop stash@{0} &&

	echo >>sparse-index/deep/new &&
	ensure_not_expanded stash -u &&
	(
		WITHOUT_UNTRACKED_TXT=1 &&
		ensure_not_expanded stash pop
	) &&

	ensure_not_expanded stash create &&
	oid=$(git -C sparse-index stash create) &&
	ensure_not_expanded stash store -m "test" $oid &&
	ensure_not_expanded reset --hard &&
	ensure_not_expanded stash pop
'

test_expect_success 'describe tested on all' '
	init_repos &&

	# Add tag to be read by describe

	run_on_all git tag -a v1.0 -m "Version 1" &&
	test_all_match git describe --dirty &&
	run_on_all rm g &&
	test_all_match git describe --dirty
'


test_expect_success 'sparse-index is not expanded: describe' '
	init_repos &&

	# Add tag to be read by describe

	git -C sparse-index tag -a v1.0 -m "Version 1" &&

	ensure_not_expanded describe --dirty &&
	echo "test" >>sparse-index/g &&
	ensure_not_expanded describe --dirty &&
	ensure_not_expanded describe
'

test_expect_success 'sparse index is not expanded: diff and diff-index' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>$1
	EOF

	# Add file within cone
	test_sparse_match git sparse-checkout set deep &&
	run_on_all ../edit-contents deep/testfile &&
	test_all_match git add deep/testfile &&
	run_on_all ../edit-contents deep/testfile &&

	test_all_match git diff &&
	test_all_match git diff --cached &&
	ensure_not_expanded diff &&
	ensure_not_expanded diff --cached &&
	ensure_not_expanded diff-index --cached HEAD &&

	# Add file outside cone
	test_all_match git reset --hard &&
	run_on_all mkdir newdirectory &&
	run_on_all ../edit-contents newdirectory/testfile &&
	test_sparse_match git sparse-checkout set newdirectory &&
	test_all_match git add newdirectory/testfile &&
	run_on_all ../edit-contents newdirectory/testfile &&
	test_sparse_match git sparse-checkout set &&

	test_all_match git diff &&
	test_all_match git diff --cached &&
	ensure_not_expanded diff &&
	ensure_not_expanded diff --cached &&
	ensure_not_expanded diff-index --cached HEAD &&

	# Merge conflict outside cone
	# The sparse checkout will report a warning that is not in the
	# full checkout, so we use `run_on_all` instead of
	# `test_all_match`
	run_on_all git reset --hard &&
	test_all_match git checkout merge-left &&
	test_all_match test_must_fail git merge merge-right &&

	test_all_match git diff &&
	test_all_match git diff --cached &&
	ensure_not_expanded diff &&
	ensure_not_expanded diff --cached &&
	ensure_not_expanded diff-index --cached HEAD
'

test_expect_success 'sparse index is not expanded: show and rev-parse' '
	init_repos &&

	ensure_not_expanded show :a &&
	ensure_not_expanded show :deep/a &&
	ensure_not_expanded rev-parse :a &&
	ensure_not_expanded rev-parse :deep/a
'

test_expect_success 'sparse index is not expanded: update-index' '
	init_repos &&

	deep_a_oid=$(git -C full-checkout rev-parse update-deep:deep/a) &&
	ensure_not_expanded update-index --cacheinfo 100644 $deep_a_oid deep/a &&

	echo "test" >sparse-index/README.md &&
	echo "test2" >sparse-index/a &&
	rm -f sparse-index/deep/a &&

	ensure_not_expanded update-index --add README.md &&
	ensure_not_expanded update-index a &&
	ensure_not_expanded update-index --remove deep/a &&

	ensure_not_expanded reset --soft update-deep &&
	ensure_not_expanded update-index --add --remove --again
'

test_expect_success 'sparse index is not expanded: blame' '
	init_repos &&

	for file in a \
			deep/a \
			deep/deeper1/a \
			deep/deeper1/deepest/a
	do
		ensure_not_expanded blame $file || return 1
	done
'

test_expect_success 'sparse index is not expanded: fetch/pull' '
	init_repos &&

	git -C sparse-index remote add full "file://$(pwd)/full-checkout" &&
	ensure_not_expanded fetch full &&
	git -C full-checkout commit --allow-empty -m "for pull merge" &&
	git -C sparse-index commit --allow-empty -m "for pull merge" &&
	ensure_not_expanded pull full base
'

test_expect_success 'sparse index is not expanded: read-tree' '
	init_repos &&

	ensure_not_expanded checkout -b test-branch update-folder1 &&
	for MERGE_TREES in "base HEAD update-folder2" \
			   "base HEAD rename-base" \
			   "base update-folder2" \
			   "base rename-base" \
			   "update-folder2"
	do
		ensure_not_expanded read-tree -mu $MERGE_TREES &&
		ensure_not_expanded reset --hard || return 1
	done &&

	rm -rf sparse-index/deep/deeper2 &&
	ensure_not_expanded add . &&
	ensure_not_expanded commit -m "test" &&

	ensure_not_expanded read-tree --prefix=deep/deeper2 -u deepest
'

test_expect_success 'ls-files' '
	init_repos &&

	# Use a smaller sparse-checkout for reduced output
	test_sparse_match git sparse-checkout set &&

	# Behavior agrees by default. Sparse index is expanded.
	test_all_match git ls-files &&

	# With --sparse, the sparse index data changes behavior.
	git -C sparse-index ls-files --sparse >actual &&

	cat >expect <<-\EOF &&
	a
	before/
	deep/
	e
	folder1-
	folder1.x
	folder1/
	folder10
	folder2/
	g
	x/
	z
	EOF

	test_cmp expect actual &&

	# With --sparse and no sparse index, nothing changes.
	git -C sparse-checkout ls-files >dense &&
	git -C sparse-checkout ls-files --sparse >sparse &&
	test_cmp dense sparse &&

	# Set up a strange condition of having a file edit
	# outside of the sparse-checkout cone. We want to verify
	# that all modes handle this the same, and detect the
	# modification.
	write_script edit-content <<-\EOF &&
	mkdir -p folder1 &&
	echo content >>folder1/a
	EOF
	run_on_all ../edit-content &&

	test_all_match git ls-files --modified &&

	git -C sparse-index ls-files --sparse --modified >sparse-index-out &&
	cat >expect <<-\EOF &&
	folder1/a
	EOF
	test_cmp expect sparse-index-out &&

	# Add folder1 to the sparse-checkout cone and
	# check that ls-files shows the expanded files.
	test_sparse_match git sparse-checkout add folder1 &&
	test_all_match git ls-files --modified &&

	test_all_match git ls-files &&
	git -C sparse-index ls-files --sparse >actual &&

	cat >expect <<-\EOF &&
	a
	before/
	deep/
	e
	folder1-
	folder1.x
	folder1/0/0/0
	folder1/0/1
	folder1/a
	folder10
	folder2/
	g
	x/
	z
	EOF

	test_cmp expect actual &&

	# Double-check index expansion is avoided
	ensure_not_expanded ls-files --sparse
'

test_expect_success 'sparse index is not expanded: sparse-checkout' '
	init_repos &&

	ensure_not_expanded sparse-checkout set deep/deeper2 &&
	ensure_not_expanded sparse-checkout set deep/deeper1 &&
	ensure_not_expanded sparse-checkout set deep &&
	ensure_not_expanded sparse-checkout add folder1 &&
	ensure_not_expanded sparse-checkout set deep/deeper1 &&
	ensure_not_expanded sparse-checkout set folder2 &&

	# Demonstrate that the checks that "folder1/a" is a file
	# do not cause a sparse-index expansion (since it is in the
	# sparse-checkout cone).
	echo >>sparse-index/folder2/a &&
	git -C sparse-index add folder2/a &&

	ensure_not_expanded sparse-checkout add folder1 &&

	# Skip checks here, since deep/deeper1 is inside a sparse directory
	# that must be expanded to check whether `deep/deeper1` is a file
	# or not.
	ensure_not_expanded sparse-checkout set --skip-checks deep/deeper1 &&
	ensure_not_expanded sparse-checkout set
'

# NEEDSWORK: a sparse-checkout behaves differently from a full checkout
# in this scenario, but it shouldn't.
test_expect_success 'reset mixed and checkout orphan' '
	init_repos &&

	test_all_match git checkout rename-out-to-in &&

	# Sparse checkouts do not agree with full checkouts about
	# how to report a directory/file conflict during a reset.
	# This command would fail with test_all_match because the
	# full checkout reports "T folder1/0/1" while a sparse
	# checkout reports "D folder1/0/1". This matches because
	# the sparse checkouts skip "adding" the other side of
	# the conflict.
	test_sparse_match git reset --mixed HEAD~1 &&
	test_sparse_match git ls-files --stage &&
	test_sparse_match git status --porcelain=v2 &&

	# At this point, sparse-checkouts behave differently
	# from the full-checkout.
	test_sparse_match git checkout --orphan new-branch &&
	test_sparse_match git ls-files --stage &&
	test_sparse_match git status --porcelain=v2
'

test_expect_success 'add everything with deep new file' '
	init_repos &&

	run_on_sparse git sparse-checkout set deep/deeper1/deepest &&

	run_on_all touch deep/deeper1/x &&
	test_all_match git add . &&
	test_all_match git status --porcelain=v2
'

# NEEDSWORK: 'git checkout' behaves incorrectly in the case of
# directory/file conflicts, even without sparse-checkout. Use this
# test only as a documentation of the incorrect behavior, not a
# measure of how it _should_ behave.
test_expect_success 'checkout behaves oddly with df-conflict-1' '
	init_repos &&

	test_sparse_match git sparse-checkout disable &&

	write_script edit-content <<-\EOF &&
	echo content >>folder1/larger-content
	git add folder1
	EOF

	run_on_all ../edit-content &&
	test_all_match git status --porcelain=v2 &&

	git -C sparse-checkout sparse-checkout init --cone &&
	git -C sparse-index sparse-checkout init --cone --sparse-index &&

	test_all_match git status --porcelain=v2 &&

	# This checkout command should fail, because we have a staged
	# change to folder1/larger-content, but the destination changes
	# folder1 to a file.
	git -C full-checkout checkout df-conflict-1 \
		1>full-checkout-out \
		2>full-checkout-err &&
	git -C sparse-checkout checkout df-conflict-1 \
		1>sparse-checkout-out \
		2>sparse-checkout-err &&
	git -C sparse-index checkout df-conflict-1 \
		1>sparse-index-out \
		2>sparse-index-err &&

	# Instead, the checkout deletes the folder1 file and adds the
	# folder1/larger-content file, leaving all other paths that were
	# in folder1/ as deleted (without any warning).
	cat >expect <<-EOF &&
	D	folder1
	A	folder1/larger-content
	EOF
	test_cmp expect full-checkout-out &&
	test_cmp expect sparse-checkout-out &&

	# The sparse-index reports no output
	test_must_be_empty sparse-index-out &&

	# stderr: Switched to branch df-conflict-1
	test_cmp full-checkout-err sparse-checkout-err &&
	test_cmp full-checkout-err sparse-checkout-err
'

# NEEDSWORK: 'git checkout' behaves incorrectly in the case of
# directory/file conflicts, even without sparse-checkout. Use this
# test only as a documentation of the incorrect behavior, not a
# measure of how it _should_ behave.
test_expect_success 'checkout behaves oddly with df-conflict-2' '
	init_repos &&

	test_sparse_match git sparse-checkout disable &&

	write_script edit-content <<-\EOF &&
	echo content >>folder2/larger-content
	git add folder2
	EOF

	run_on_all ../edit-content &&
	test_all_match git status --porcelain=v2 &&

	git -C sparse-checkout sparse-checkout init --cone &&
	git -C sparse-index sparse-checkout init --cone --sparse-index &&

	test_all_match git status --porcelain=v2 &&

	# This checkout command should fail, because we have a staged
	# change to folder1/larger-content, but the destination changes
	# folder1 to a file.
	git -C full-checkout checkout df-conflict-2 \
		1>full-checkout-out \
		2>full-checkout-err &&
	git -C sparse-checkout checkout df-conflict-2 \
		1>sparse-checkout-out \
		2>sparse-checkout-err &&
	git -C sparse-index checkout df-conflict-2 \
		1>sparse-index-out \
		2>sparse-index-err &&

	# The full checkout deviates from the df-conflict-1 case here!
	# It drops the change to folder1/larger-content and leaves the
	# folder1 path as-is on disk. The sparse-index behaves the same.
	test_must_be_empty full-checkout-out &&
	test_must_be_empty sparse-index-out &&

	# In the sparse-checkout case, the checkout deletes the folder1
	# file and adds the folder1/larger-content file, leaving all other
	# paths that were in folder1/ as deleted (without any warning).
	cat >expect <<-EOF &&
	D	folder2
	A	folder2/larger-content
	EOF
	test_cmp expect sparse-checkout-out &&

	# Switched to branch df-conflict-1
	test_cmp full-checkout-err sparse-checkout-err &&
	test_cmp full-checkout-err sparse-index-err
'

test_expect_success 'mv directory from out-of-cone to in-cone' '
	init_repos &&

	# <source> as a sparse directory (or SKIP_WORKTREE_DIR without enabling
	# sparse index).
	test_all_match git mv --sparse folder1 deep &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match git ls-files -t &&
	git -C sparse-checkout ls-files -t >actual &&
	grep -e "H deep/folder1/0/0/0" actual &&
	grep -e "H deep/folder1/0/1" actual &&
	grep -e "H deep/folder1/a" actual &&

	test_all_match git reset --hard &&

	# <source> as a directory deeper than sparse index boundary (where
	# sparse index will expand).
	test_sparse_match git mv --sparse folder1/0 deep &&
	test_sparse_match git status --porcelain=v2 &&
	test_sparse_match git ls-files -t &&
	git -C sparse-checkout ls-files -t >actual &&
	grep -e "H deep/0/0/0" actual &&
	grep -e "H deep/0/1" actual
'

test_expect_success 'rm pathspec inside sparse definition' '
	init_repos &&

	test_all_match git rm deep/a &&
	test_all_match git status --porcelain=v2 &&

	# test wildcard
	run_on_all git reset --hard &&
	test_all_match git rm deep/* &&
	test_all_match git status --porcelain=v2 &&

	# test recursive rm
	run_on_all git reset --hard &&
	test_all_match git rm -r deep &&
	test_all_match git status --porcelain=v2
'

test_expect_success 'rm pathspec outside sparse definition' '
	init_repos &&

	for file in folder1/a folder1/0/1
	do
		test_sparse_match test_must_fail git rm $file &&
		test_sparse_match test_must_fail git rm --cached $file &&
		test_sparse_match git rm --sparse $file &&
		test_sparse_match git status --porcelain=v2 || return 1
	done &&

	cat >folder1-full <<-EOF &&
	rm ${SQ}folder1/0/0/0${SQ}
	rm ${SQ}folder1/0/1${SQ}
	rm ${SQ}folder1/a${SQ}
	EOF

	cat >folder1-sparse <<-EOF &&
	rm ${SQ}folder1/${SQ}
	EOF

	# test wildcard
	run_on_sparse git reset --hard &&
	run_on_sparse git sparse-checkout reapply &&
	test_sparse_match test_must_fail git rm folder1/* &&
	run_on_sparse git rm --sparse folder1/* &&
	test_cmp folder1-full sparse-checkout-out &&
	test_cmp folder1-sparse sparse-index-out &&
	test_sparse_match git status --porcelain=v2 &&

	# test recursive rm
	run_on_sparse git reset --hard &&
	run_on_sparse git sparse-checkout reapply &&
	test_sparse_match test_must_fail git rm --sparse folder1 &&
	run_on_sparse git rm --sparse -r folder1 &&
	test_cmp folder1-full sparse-checkout-out &&
	test_cmp folder1-sparse sparse-index-out &&
	test_sparse_match git status --porcelain=v2
'

test_expect_success 'rm pathspec expands index when necessary' '
	init_repos &&

	# in-cone pathspec (do not expand)
	ensure_not_expanded rm "deep/deep*" &&
	test_must_be_empty sparse-index-err &&

	# out-of-cone pathspec (expand)
	! ensure_not_expanded rm --sparse "folder1/a*" &&
	test_must_be_empty sparse-index-err &&

	# pathspec that should expand index
	! ensure_not_expanded rm "*/a" &&
	test_must_be_empty sparse-index-err &&

	! ensure_not_expanded rm "**a" &&
	test_must_be_empty sparse-index-err
'

test_expect_success 'sparse index is not expanded: rm' '
	init_repos &&

	ensure_not_expanded rm deep/a &&

	# test in-cone wildcard
	git -C sparse-index reset --hard &&
	ensure_not_expanded rm deep/* &&

	# test recursive rm
	git -C sparse-index reset --hard &&
	ensure_not_expanded rm -r deep
'

test_expect_success 'grep with and --cached' '
	init_repos &&

	test_all_match git grep --cached a &&
	test_all_match git grep --cached a -- "folder1/*"
'

test_expect_success 'grep is not expanded' '
	init_repos &&

	ensure_not_expanded grep a &&
	ensure_not_expanded grep a -- deep/* &&

	# All files within the folder1/* pathspec are sparse,
	# so this command does not find any matches
	ensure_not_expanded ! grep a -- folder1/* &&

	# test out-of-cone pathspec with or without wildcard
	ensure_not_expanded grep --cached a -- "folder1/a" &&
	ensure_not_expanded grep --cached a -- "folder1/*" &&

	# test in-cone pathspec with or without wildcard
	ensure_not_expanded grep --cached a -- "deep/a" &&
	ensure_not_expanded grep --cached a -- "deep/*"
'

# NEEDSWORK: when running `grep` in the superproject with --recurse-submodules,
# Git expands the index of the submodules unexpectedly. Even though `grep`
# builtin is marked as "command_requires_full_index = 0", this config is only
# useful for the superproject. Namely, the submodules have their own configs,
# which are _not_ populated by the one-time sparse-index feature switch.
test_expect_failure 'grep within submodules is not expanded' '
	init_repos_as_submodules &&

	# do not use ensure_not_expanded() here, becasue `grep` should be
	# run in the superproject, not in "./sparse-index"
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
	git grep --cached --recurse-submodules a -- "*/folder1/*" &&
	test_region ! index ensure_full_index trace2.txt
'

# NEEDSWORK: this test is not actually testing the code. The design purpose
# of this test is to verify the grep result when the submodules are using a
# sparse-index. Namely, we want "folder1/" as a tree (a sparse directory); but
# because of the index expansion, we are now grepping the "folder1/a" blob.
# Because of the problem stated above 'grep within submodules is not expanded',
# we don't have the ideal test environment yet.
test_expect_success 'grep sparse directory within submodules' '
	init_repos_as_submodules &&

	cat >expect <<-\EOF &&
	full-checkout/folder1/a:a
	sparse-checkout/folder1/a:a
	sparse-index/folder1/a:a
	EOF
	git grep --cached --recurse-submodules a -- "*/folder1/*" >actual &&
	test_cmp actual expect
'

test_expect_success 'write-tree' '
	init_repos &&

	test_all_match git write-tree &&

	write_script edit-contents <<-\EOF &&
	echo text >>"$1"
	EOF

	# make a change inside the sparse cone
	run_on_all ../edit-contents deep/a &&
	test_all_match git update-index deep/a &&
	test_all_match git write-tree &&
	test_all_match git status --porcelain=v2 &&

	# make a change outside the sparse cone
	run_on_all mkdir -p folder1 &&
	run_on_all cp a folder1/a &&
	run_on_all ../edit-contents folder1/a &&
	test_all_match git update-index folder1/a &&
	test_all_match git write-tree &&
	test_all_match git status --porcelain=v2 &&

	# check that SKIP_WORKTREE files are not materialized
	test_path_is_missing sparse-checkout/folder2/a &&
	test_path_is_missing sparse-index/folder2/a
'

test_expect_success 'sparse-index is not expanded: write-tree' '
	init_repos &&

	ensure_not_expanded write-tree &&

	echo "test1" >>sparse-index/a &&
	git -C sparse-index update-index a &&
	ensure_not_expanded write-tree
'

test_expect_success 'diff-files with pathspec inside sparse definition' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>"$1"
	EOF

	run_on_all ../edit-contents deep/a &&

	test_all_match git diff-files &&

	test_all_match git diff-files -- deep/a &&

	# test wildcard
	test_all_match git diff-files -- "deep/*"
'

test_expect_success 'diff-files with pathspec outside sparse definition' '
	init_repos &&

	test_sparse_match git diff-files -- folder2/a &&

	write_script edit-contents <<-\EOF &&
	echo text >>"$1"
	EOF

	# The directory "folder1" is outside the cone of interest
	# and will not exist in the sparse checkout repositories.
	# Create it as needed, add file "folder1/a" there with
	# contents that is different from the staged version.
	run_on_all mkdir -p folder1 &&
	run_on_all cp a folder1/a &&

	run_on_all ../edit-contents folder1/a &&
	test_all_match git diff-files &&
	test_all_match git diff-files -- folder1/a &&
	test_all_match git diff-files -- "folder*/a"
'

test_expect_success 'sparse index is not expanded: diff-files' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>"$1"
	EOF

	run_on_all ../edit-contents deep/a &&

	ensure_not_expanded diff-files &&
	ensure_not_expanded diff-files -- deep/a &&
	ensure_not_expanded diff-files -- "deep/*"
'

test_expect_success 'diff-tree' '
	init_repos &&

	# Test change inside sparse cone
	tree1=$(git -C sparse-index rev-parse HEAD^{tree}) &&
	tree2=$(git -C sparse-index rev-parse update-deep^{tree}) &&
	test_all_match git diff-tree $tree1 $tree2 &&
	test_all_match git diff-tree $tree1 $tree2 -- deep/a &&
	test_all_match git diff-tree HEAD update-deep &&
	test_all_match git diff-tree HEAD update-deep -- deep/a &&

	# Test change outside sparse cone
	tree3=$(git -C sparse-index rev-parse update-folder1^{tree}) &&
	test_all_match git diff-tree $tree1 $tree3 &&
	test_all_match git diff-tree $tree1 $tree3 -- folder1/a &&
	test_all_match git diff-tree HEAD update-folder1 &&
	test_all_match git diff-tree HEAD update-folder1 -- folder1/a &&

	# Check that SKIP_WORKTREE files are not materialized
	test_path_is_missing sparse-checkout/folder1/a &&
	test_path_is_missing sparse-index/folder1/a &&
	test_path_is_missing sparse-checkout/folder2/a &&
	test_path_is_missing sparse-index/folder2/a
'

test_expect_success 'sparse-index is not expanded: diff-tree' '
	init_repos &&

	tree1=$(git -C sparse-index rev-parse HEAD^{tree}) &&
	tree2=$(git -C sparse-index rev-parse update-deep^{tree}) &&
	tree3=$(git -C sparse-index rev-parse update-folder1^{tree}) &&

	ensure_not_expanded diff-tree $tree1 $tree2 &&
	ensure_not_expanded diff-tree $tree1 $tree2 -- deep/a &&
	ensure_not_expanded diff-tree HEAD update-deep &&
	ensure_not_expanded diff-tree HEAD update-deep -- deep/a &&
	ensure_not_expanded diff-tree $tree1 $tree3 &&
	ensure_not_expanded diff-tree $tree1 $tree3 -- folder1/a &&
	ensure_not_expanded diff-tree HEAD update-folder1 &&
	ensure_not_expanded diff-tree HEAD update-folder1 -- folder1/a
'

test_expect_success 'worktree' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>"$1"
	EOF

	for repo in full-checkout sparse-checkout sparse-index
	do
		worktree=${repo}-wt &&
		git -C $repo worktree add ../$worktree &&

		# Compare worktree content with "ls"
		(cd $repo && ls) >worktree_contents &&
		(cd $worktree && ls) >new_worktree_contents &&
		test_cmp worktree_contents new_worktree_contents &&

		# Compare index content with "ls-files --sparse"
		git -C $repo ls-files --sparse >index_contents &&
		git -C $worktree ls-files --sparse >new_index_contents &&
		test_cmp index_contents new_index_contents &&

		git -C $repo worktree remove ../$worktree || return 1
	done &&

	test_all_match git worktree add .worktrees/hotfix &&
	run_on_all ../edit-contents .worktrees/hotfix/deep/a &&
	test_all_match test_must_fail git worktree remove .worktrees/hotfix
'

test_expect_success 'worktree is not expanded' '
	init_repos &&

	ensure_not_expanded worktree add .worktrees/hotfix &&
	ensure_not_expanded worktree remove .worktrees/hotfix
'

test_expect_success 'check-attr with pathspec inside sparse definition' '
	init_repos &&

	echo "a -crlf myAttr" >>.gitattributes &&
	run_on_all cp ../.gitattributes ./deep &&

	test_all_match git check-attr -a -- deep/a &&

	test_all_match git add deep/.gitattributes &&
	test_all_match git check-attr -a --cached -- deep/a
'

test_expect_success 'check-attr with pathspec outside sparse definition' '
	init_repos &&

	echo "a -crlf myAttr" >>.gitattributes &&
	run_on_sparse mkdir folder1 &&
	run_on_all cp ../.gitattributes ./folder1 &&
	run_on_all cp a folder1/a &&

	test_all_match git check-attr -a -- folder1/a &&

	git -C full-checkout add folder1/.gitattributes &&
	test_sparse_match git add --sparse folder1/.gitattributes &&
	test_all_match git commit -m "add .gitattributes" &&
	test_sparse_match git sparse-checkout reapply &&
	test_all_match git check-attr -a --cached -- folder1/a
'

# NEEDSWORK: The 'diff --check' test is left as 'test_expect_failure' due
# to an underlying issue in oneway_diff() within diff-lib.c.
# 'do_oneway_diff()' is not called as expected for paths that could match
# inside of a sparse directory. Specifically, the 'ce_path_match()' function
# fails to recognize files inside a sparse directory (e.g., when 'folder1/'
# is a sparse directory, 'folder1/a' cannot be recognized). The goal is to
# proceed with 'do_oneway_diff()' if the pathspec could match inside of a
# sparse directory.
test_expect_failure 'diff --check with pathspec outside sparse definition' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo "a " >"$1"
	EOF

	test_all_match git config core.whitespace -trailing-space,-space-before-tab &&

	echo "a whitespace=trailing-space,space-before-tab" >>.gitattributes &&
	run_on_all mkdir -p folder1 &&
	run_on_all cp ../.gitattributes ./folder1 &&
	test_all_match git add --sparse folder1/.gitattributes &&
	run_on_all ../edit-contents folder1/a &&
	test_all_match git add --sparse folder1/a &&

	test_sparse_match git sparse-checkout reapply &&
	test_all_match test_must_fail git diff --check --cached -- folder1/a
'

test_expect_success 'sparse-index is not expanded: check-attr' '
	init_repos &&

	echo "a -crlf myAttr" >>.gitattributes &&
	mkdir ./sparse-index/folder1 &&
	cp ./sparse-index/a ./sparse-index/folder1/a &&
	cp .gitattributes ./sparse-index/deep &&
	cp .gitattributes ./sparse-index/folder1 &&

	git -C sparse-index add deep/.gitattributes &&
	git -C sparse-index add --sparse folder1/.gitattributes &&
	ensure_not_expanded check-attr -a --cached -- deep/a &&
	ensure_not_expanded check-attr -a --cached -- folder1/a
'

test_expect_success 'advice.sparseIndexExpanded' '
	init_repos &&

	git -C sparse-index config --unset advice.sparseIndexExpanded &&
	git -C sparse-index sparse-checkout set deep/deeper1 &&
	mkdir -p sparse-index/deep/deeper2/deepest &&
	touch sparse-index/deep/deeper2/deepest/bogus &&
	git -C sparse-index status 2>err &&
	grep "The sparse index is expanding to a full index" err
'

test_expect_success 'cat-file -p' '
	init_repos &&
	echo "new content" >>full-checkout/deep/a &&
	echo "new content" >>sparse-checkout/deep/a &&
	echo "new content" >>sparse-index/deep/a &&
	run_on_all git add deep/a &&

	test_all_match git cat-file -p :deep/a &&
	ensure_not_expanded cat-file -p :deep/a &&
	test_all_match git cat-file -p :folder1/a &&
	ensure_expanded cat-file -p :folder1/a
'

test_expect_success 'cat-file --batch' '
	init_repos &&
	echo "new content" >>full-checkout/deep/a &&
	echo "new content" >>sparse-checkout/deep/a &&
	echo "new content" >>sparse-index/deep/a &&
	run_on_all git add deep/a &&

	echo ":deep/a" >in &&
	test_all_match git cat-file --batch <in &&
	ensure_not_expanded cat-file --batch <in &&

	echo ":folder1/a" >in &&
	test_all_match git cat-file --batch <in &&
	ensure_expanded cat-file --batch <in &&

	cat >in <<-\EOF &&
	:deep/a
	:folder1/a
	EOF
	test_all_match git cat-file --batch <in &&
	ensure_expanded cat-file --batch <in
'

test_done
