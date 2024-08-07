#!/bin/sh
#
# Copyright (c) 2009 Eric Wong

test_description='git svn creates empty directories'

. ./lib-git-svn.sh

test_expect_success 'initialize repo' '
	for i in a b c d d/e d/e/f "weird file name"
	do
		svn_cmd mkdir -m "mkdir $i" "$svnrepo"/"$i" || return 1
	done
'

test_expect_success 'clone' 'git svn clone "$svnrepo" cloned'

test_expect_success 'empty directories exist' '
	(
		cd cloned &&
		for i in a b c d d/e d/e/f "weird file name"
		do
			test_path_is_dir "$i" || exit 1
		done
	)
'

test_expect_success 'option automkdirs set to false' '
	(
		git svn init "$svnrepo" cloned-no-mkdirs &&
		cd cloned-no-mkdirs &&
		git config svn-remote.svn.automkdirs false &&
		git svn fetch &&
		for i in a b c d d/e d/e/f "weird file name"
		do
			test_path_is_missing "$i" || exit 1
		done
	)
'

test_expect_success 'more emptiness' '
	svn_cmd mkdir -m "bang bang"  "$svnrepo"/"! !"
'

test_expect_success 'git svn rebase creates empty directory' '
	( cd cloned && git svn rebase ) &&
	test_path_is_dir cloned/"! !"
'

test_expect_success 'git svn mkdirs recreates empty directories' '
	(
		cd cloned &&
		rm -r * &&
		git svn mkdirs &&
		for i in a b c d d/e d/e/f "weird file name" "! !"
		do
			test_path_is_dir "$i" || exit 1
		done
	)
'

test_expect_success 'git svn mkdirs -r works' '
	(
		cd cloned &&
		rm -r * &&
		git svn mkdirs -r7 &&
		for i in a b c d d/e d/e/f "weird file name"
		do
			test_path_is_dir "$i" || exit 1
		done &&

		test_path_is_missing "! !" || exit 1 &&

		git svn mkdirs -r8 &&
		test_path_is_dir "! !" || exit 1
	)
'

test_expect_success 'initialize trunk' '
	for i in trunk trunk/a trunk/"weird file name"
	do
		svn_cmd mkdir -m "mkdir $i" "$svnrepo"/"$i" || return 1
	done
'

test_expect_success 'clone trunk' 'git svn clone -s "$svnrepo" trunk'

test_expect_success 'empty directories in trunk exist' '
	(
		cd trunk &&
		for i in a "weird file name"
		do
			test_path_is_dir "$i" || exit 1
		done
	)
'

test_expect_success 'remove a top-level directory from svn' '
	svn_cmd rm -m "remove d" "$svnrepo"/d
'

test_expect_success 'removed top-level directory does not exist' '
	git svn clone "$svnrepo" removed &&
	test_path_is_missing removed/d

'
unhandled=.git/svn/refs/remotes/git-svn/unhandled.log
test_expect_success 'git svn gc-ed files work' '
	(
		cd removed &&
		git svn gc &&
		: Compress::Zlib may not be available &&
		if test -f "$unhandled".gz
		then
			svn_cmd mkdir -m gz "$svnrepo"/gz &&
			git reset --hard $(git rev-list HEAD | tail -1) &&
			git svn rebase &&
			test_path_is_file "$unhandled".gz &&
			test_path_is_file "$unhandled" &&
			for i in a b c "weird file name" gz "! !"
			do
				test_path_is_dir "$i" || exit 1
			done
		fi
	)
'

test_done
