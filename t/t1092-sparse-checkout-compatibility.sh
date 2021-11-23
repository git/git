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
		mkdir folder1 folder2 deep x &&
		mkdir deep/deeper1 deep/deeper2 deep/before deep/later &&
		mkdir deep/deeper1/deepest &&
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
		cp -r deep/deeper1/deepest deep/deeper2 &&
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
	git -C sparse-index sparse-checkout set deep
}

run_on_sparse () {
	(
		cd sparse-checkout &&
		GIT_PROGRESS_DELAY=100000 "$@" >../sparse-checkout-out 2>../sparse-checkout-err
	) &&
	(
		cd sparse-index &&
		GIT_PROGRESS_DELAY=100000 "$@" >../sparse-index-out 2>../sparse-index-err
	)
}

run_on_all () {
	(
		cd full-checkout &&
		GIT_PROGRESS_DELAY=100000 "$@" >../full-checkout-out 2>../full-checkout-err
	) &&
	run_on_sparse "$@"
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

test_expect_success 'sparse-index contents' '
	init_repos &&

	test-tool -C sparse-index read-cache --table >cache &&
	for dir in folder1 folder2 x
	do
		TREE=$(git -C sparse-index rev-parse HEAD:$dir) &&
		grep "040000 tree $TREE	$dir/" cache \
			|| return 1
	done &&

	git -C sparse-index sparse-checkout set folder1 &&

	test-tool -C sparse-index read-cache --table >cache &&
	for dir in deep folder2 x
	do
		TREE=$(git -C sparse-index rev-parse HEAD:$dir) &&
		grep "040000 tree $TREE	$dir/" cache \
			|| return 1
	done &&

	git -C sparse-index sparse-checkout set deep/deeper1 &&

	test-tool -C sparse-index read-cache --table >cache &&
	for dir in deep/deeper2 folder1 folder2 x
	do
		TREE=$(git -C sparse-index rev-parse HEAD:$dir) &&
		grep "040000 tree $TREE	$dir/" cache \
			|| return 1
	done &&

	# Disabling the sparse-index removes tree entries with full ones
	git -C sparse-index sparse-checkout init --no-sparse-index &&

	test-tool -C sparse-index read-cache --table >cache &&
	! grep "040000 tree" cache &&
	test_sparse_match test-tool read-cache --table
'

test_expect_success 'expanded in-memory index matches full index' '
	init_repos &&
	test_sparse_match test-tool read-cache --expand --table
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

test_expect_success 'status reports sparse-checkout' '
	init_repos &&
	git -C sparse-checkout status >full &&
	git -C sparse-index status >sparse &&
	test_i18ngrep "You are in a sparse checkout with " full &&
	test_i18ngrep "You are in a sparse checkout." sparse
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
	run_on_sparse ../edit-contents folder1/a &&
	run_on_all ../edit-contents folder1/new &&

	test_sparse_match git status --porcelain=v2 &&

	# Adding the path outside of the sparse-checkout cone should fail.
	test_sparse_match test_must_fail git add folder1/a &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
	test_sparse_unstaged folder1/a &&
	test_sparse_match test_must_fail git add --refresh folder1/a &&
	grep "Disable or modify the sparsity rules" sparse-checkout-err &&
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

test_expect_success 'diff --staged' '
	init_repos &&

	write_script edit-contents <<-\EOF &&
	echo text >>README.md
	EOF
	run_on_all ../edit-contents &&

	test_all_match git diff &&
	test_all_match git diff --staged &&
	test_all_match git add README.md &&
	test_all_match git diff &&
	test_all_match git diff --staged
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
		test_all_match git diff --staged --no-renames &&
		test_all_match git diff --staged --find-renames || return 1
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
		test_all_match git diff --staged --no-renames &&
		test_all_match git diff --staged --find-renames || return 1
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

	test_all_match git blame a &&
	test_all_match git blame deep/a &&
	test_all_match git blame deep/deeper1/a &&
	test_all_match git blame deep/deeper1/deepest/a
'

# TODO: blame currently does not support blaming files outside of the
# sparse definition. It complains that the file doesn't exist locally.
test_expect_failure 'blame with pathspec outside sparse definition' '
	init_repos &&

	test_all_match git blame folder1/a &&
	test_all_match git blame folder2/a &&
	test_all_match git blame deep/deeper2/a &&
	test_all_match git blame deep/deeper2/deepest/a
'

# NEEDSWORK: a sparse-checkout behaves differently from a full checkout
# in this scenario, but it shouldn't.
test_expect_failure 'checkout and reset (mixed)' '
	init_repos &&

	test_all_match git checkout -b reset-test update-deep &&
	test_all_match git reset deepest &&
	test_all_match git reset update-folder1 &&
	test_all_match git reset update-folder2
'

# NEEDSWORK: a sparse-checkout behaves differently from a full checkout
# in this scenario, but it shouldn't.
test_expect_success 'checkout and reset (mixed) [sparse]' '
	init_repos &&

	test_sparse_match git checkout -b reset-test update-deep &&
	test_sparse_match git reset deepest &&
	test_sparse_match git reset update-folder1 &&
	test_sparse_match git reset update-folder2
'

test_expect_success 'merge, cherry-pick, and rebase' '
	init_repos &&

	for OPERATION in "merge -m merge" cherry-pick rebase
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

test_expect_success 'clean' '
	init_repos &&

	echo bogus >>.gitignore &&
	run_on_all cp ../.gitignore . &&
	test_all_match git add .gitignore &&
	test_all_match git commit -m "ignore bogus files" &&

	run_on_sparse mkdir folder1 &&
	run_on_all touch folder1/bogus &&

	test_all_match git status --porcelain=v2 &&
	test_all_match git clean -f &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&

	test_all_match git clean -xf &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&

	test_all_match git clean -xdf &&
	test_all_match git status --porcelain=v2 &&
	test_sparse_match ls &&
	test_sparse_match ls folder1 &&

	test_sparse_match test_path_is_dir folder1
'

test_expect_success 'submodule handling' '
	init_repos &&

	test_sparse_match git sparse-checkout add modules &&
	test_all_match mkdir modules &&
	test_all_match touch modules/a &&
	test_all_match git add modules &&
	test_all_match git commit -m "add modules directory" &&

	run_on_all git submodule add "$(pwd)/initial-repo" modules/sub &&
	test_all_match git commit -m "add submodule" &&

	# having a submodule prevents "modules" from collapse
	test_sparse_match git sparse-checkout set deep/deeper1 &&
	test-tool -C sparse-index read-cache --table >cache &&
	grep "100644 blob .*	modules/a" cache &&
	grep "160000 commit $(git -C initial-repo rev-parse HEAD)	modules/sub" cache
'

test_expect_success 'sparse-index is expanded and converted back' '
	init_repos &&

	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
		git -C sparse-index -c core.fsmonitor="" reset --hard &&
	test_region index convert_to_sparse trace2.txt &&
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

ensure_not_expanded () {
	rm -f trace2.txt &&
	echo >>sparse-index/untracked.txt &&

	if test "$1" = "!"
	then
		shift &&
		test_must_fail env \
			GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
			git -C sparse-index "$@" || return 1
	else
		GIT_TRACE2_EVENT="$(pwd)/trace2.txt" GIT_TRACE2_EVENT_NESTING=10 \
			git -C sparse-index "$@" || return 1
	fi &&
	test_region ! index ensure_full_index trace2.txt
}

test_expect_success 'sparse-index is not expanded' '
	init_repos &&

	ensure_not_expanded status &&
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
	git -C sparse-index reset --hard &&
	ensure_not_expanded checkout rename-out-to-out -- deep/deeper1 &&
	git -C sparse-index reset --hard &&
	ensure_not_expanded restore -s rename-out-to-out -- deep/deeper1 &&

	echo >>sparse-index/README.md &&
	ensure_not_expanded add -A &&
	echo >>sparse-index/extra.txt &&
	ensure_not_expanded add extra.txt &&
	echo >>sparse-index/untracked.txt &&
	ensure_not_expanded add . &&

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
	test_sparse_match test-tool read-cache --table --expand &&
	test_sparse_match git status --porcelain=v2 &&

	# At this point, sparse-checkouts behave differently
	# from the full-checkout.
	test_sparse_match git checkout --orphan new-branch &&
	test_sparse_match test-tool read-cache --table --expand &&
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

test_done
