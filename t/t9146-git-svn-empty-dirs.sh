#!/bin/sh
#
# Copyright (c) 2009 Eric Wong

test_description='git svn creates empty directories'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' '
	for i in a b c d d/e d/e/f "weird file name"
	do
		svn_cmd mkdir -m "mkdir $i" "$svnrepo"/"$i"
	done
'

test_expect_success 'clone' 'git svn clone "$svnrepo" cloned'

test_expect_success 'empty directories exist' '
	(
		cd cloned &&
		for i in a b c d d/e d/e/f "weird file name"
		do
			if ! test -d "$i"
			then
				echo >&2 "$i does not exist"
				exit 1
			fi
		done
	)
'

test_expect_success 'more emptiness' '
	svn_cmd mkdir -m "bang bang"  "$svnrepo"/"! !"
'

test_expect_success 'git svn rebase creates empty directory' '
	( cd cloned && git svn rebase )
	test -d cloned/"! !"
'

test_expect_success 'git svn mkdirs recreates empty directories' '
	(
		cd cloned &&
		rm -r * &&
		git svn mkdirs &&
		for i in a b c d d/e d/e/f "weird file name" "! !"
		do
			if ! test -d "$i"
			then
				echo >&2 "$i does not exist"
				exit 1
			fi
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
			if ! test -d "$i"
			then
				echo >&2 "$i does not exist"
				exit 1
			fi
		done

		if test -d "! !"
		then
			echo >&2 "$i should not exist"
			exit 1
		fi

		git svn mkdirs -r8 &&
		if ! test -d "! !"
		then
			echo >&2 "$i not exist"
			exit 1
		fi
	)
'

test_done
