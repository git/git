#!/bin/sh

test_description='parallel-checkout basics

Ensure that parallel-checkout basically works on clone and checkout, spawning
the required number of workers and correctly populating both the index and the
working tree.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-parallel-checkout.sh"

# Test parallel-checkout with a branch switch containing a variety of file
# creations, deletions, and modifications, involving different entry types.
# The branches B1 and B2 have the following paths:
#
#      B1                 B2
#  a/a (file)         a   (file)
#  b   (file)         b/b (file)
#
#  c/c (file)         c   (symlink)
#  d   (symlink)      d/d (file)
#
#  e/e (file)         e   (submodule)
#  f   (submodule)    f/f (file)
#
#  g   (submodule)    g   (symlink)
#  h   (symlink)      h   (submodule)
#
# Additionally, the following paths are present on both branches, but with
# different contents:
#
#  i   (file)         i   (file)
#  j   (symlink)      j   (symlink)
#  k   (submodule)    k   (submodule)
#
# And the following paths are only present in one of the branches:
#
#  l/l (file)         -
#  -                  m/m (file)
#
test_expect_success 'setup repo for checkout with various types of changes' '
	git init sub &&
	(
		cd sub &&
		git checkout -b B2 &&
		echo B2 >file &&
		git add file &&
		git commit -m file &&

		git checkout -b B1 &&
		echo B1 >file &&
		git add file &&
		git commit -m file
	) &&

	git init various &&
	(
		cd various &&

		git checkout -b B1 &&
		mkdir a c e &&
		echo a/a >a/a &&
		echo b >b &&
		echo c/c >c/c &&
		test_ln_s_add c d &&
		echo e/e >e/e &&
		git submodule add ../sub f &&
		git submodule add ../sub g &&
		test_ln_s_add c h &&

		echo "B1 i" >i &&
		test_ln_s_add c j &&
		git submodule add -b B1 ../sub k &&
		mkdir l &&
		echo l/l >l/l &&

		git add . &&
		git commit -m B1 &&

		git checkout -b B2 &&
		git rm -rf :^.gitmodules :^k &&
		mkdir b d f &&
		echo a >a &&
		echo b/b >b/b &&
		test_ln_s_add b c &&
		echo d/d >d/d &&
		git submodule add ../sub e &&
		echo f/f >f/f &&
		test_ln_s_add b g &&
		git submodule add ../sub h &&

		echo "B2 i" >i &&
		test_ln_s_add b j &&
		git -C k checkout B2 &&
		mkdir m &&
		echo m/m >m/m &&

		git add . &&
		git commit -m B2 &&

		git checkout --recurse-submodules B1
	)
'

for mode in sequential parallel sequential-fallback
do
	case $mode in
	sequential)          workers=1 threshold=0 expected_workers=0 ;;
	parallel)            workers=2 threshold=0 expected_workers=2 ;;
	sequential-fallback) workers=2 threshold=100 expected_workers=0 ;;
	esac

	test_expect_success "$mode checkout" '
		repo=various_$mode &&
		cp -R -P various $repo &&

		# The just copied files have more recent timestamps than their
		# associated index entries. So refresh the cached timestamps
		# to avoid an "entry not up-to-date" error from `git checkout`.
		# We only have to do this for the submodules as `git checkout`
		# will already refresh the superproject index before performing
		# the up-to-date check.
		#
		git -C $repo submodule foreach "git update-index --refresh" &&

		set_checkout_config $workers $threshold &&
		test_checkout_workers $expected_workers \
			git -C $repo checkout --recurse-submodules B2 &&
		verify_checkout $repo
	'
done

for mode in parallel sequential-fallback
do
	case $mode in
	parallel)            workers=2 threshold=0 expected_workers=2 ;;
	sequential-fallback) workers=2 threshold=100 expected_workers=0 ;;
	esac

	test_expect_success "$mode checkout on clone" '
		repo=various_${mode}_clone &&
		set_checkout_config $workers $threshold &&
		test_checkout_workers $expected_workers \
			git clone --recurse-submodules --branch B2 various $repo &&
		verify_checkout $repo
	'
done

# Just to be paranoid, actually compare the working trees' contents directly.
test_expect_success 'compare the working trees' '
	rm -rf various_*/.git &&
	rm -rf various_*/*/.git &&

	# We use `git diff` instead of `diff -r` because the latter would
	# follow symlinks, and not all `diff` implementations support the
	# `--no-dereference` option.
	#
	git diff --no-index various_sequential various_parallel &&
	git diff --no-index various_sequential various_parallel_clone &&
	git diff --no-index various_sequential various_sequential-fallback &&
	git diff --no-index various_sequential various_sequential-fallback_clone
'

# Currently, each submodule is checked out in a separated child process, but
# these subprocesses must also be able to use parallel checkout workers to
# write the submodules' entries.
test_expect_success 'submodules can use parallel checkout' '
	set_checkout_config 2 0 &&
	git init super &&
	(
		cd super &&
		git init sub &&
		test_commit -C sub A &&
		test_commit -C sub B &&
		git submodule add ./sub &&
		git commit -m sub &&
		rm sub/* &&
		test_checkout_workers 2 git checkout --recurse-submodules .
	)
'

test_expect_success 'parallel checkout respects --[no]-force' '
	set_checkout_config 2 0 &&
	git init dirty &&
	(
		cd dirty &&
		mkdir D &&
		test_commit D/F &&
		test_commit F &&

		rm -rf D &&
		echo changed >D &&
		echo changed >F.t &&

		# We expect 0 workers because there is nothing to be done
		test_checkout_workers 0 git checkout HEAD &&
		test_path_is_file D &&
		grep changed D &&
		grep changed F.t &&

		test_checkout_workers 2 git checkout --force HEAD &&
		test_path_is_dir D &&
		grep D/F D/F.t &&
		grep F F.t
	)
'

test_expect_success SYMLINKS 'parallel checkout checks for symlinks in leading dirs' '
	set_checkout_config 2 0 &&
	git init symlinks &&
	(
		cd symlinks &&
		mkdir D untracked &&
		# Commit 2 files to have enough work for 2 parallel workers
		test_commit D/A &&
		test_commit D/B &&
		rm -rf D &&
		ln -s untracked D &&

		test_checkout_workers 2 git checkout --force HEAD &&
		! test -h D &&
		grep D/A D/A.t &&
		grep D/B D/B.t
	)
'

test_done
