#!/bin/sh
#
# Copyright (c) 2009 Robert Zeh

test_description='git svn creates empty directories, calls git gc, makes sure they are still empty'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' '
	for i in a b c d d/e d/e/f "weird file name"
	do
		svn_cmd mkdir -m "mkdir $i" "$svnrepo"/"$i" || return 1
	done
'

test_expect_success 'clone' 'git svn clone "$svnrepo" cloned'

test_expect_success 'git svn gc runs' '
	(
		cd cloned &&
		git svn gc
	)
'

test_expect_success 'git svn mkdirs recreates empty directories after git svn gc' '
	(
		cd cloned &&
		rm -r * &&
		git svn mkdirs &&
		for i in a b c d d/e d/e/f "weird file name"
		do
			if ! test -d "$i"
			then
				echo >&2 "$i does not exist" &&
				exit 1
			fi
		done
	)
'

test_done
