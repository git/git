#!/bin/sh
#
# Copyright (c) 2009 Robert Zeh

test_description='but svn creates empty directories, calls but gc, makes sure they are still empty'
. ./lib-but-svn.sh

test_expect_success 'initialize repo' '
	for i in a b c d d/e d/e/f "weird file name"
	do
		svn_cmd mkdir -m "mkdir $i" "$svnrepo"/"$i" || return 1
	done
'

test_expect_success 'clone' 'but svn clone "$svnrepo" cloned'

test_expect_success 'but svn gc runs' '
	(
		cd cloned &&
		but svn gc
	)
'

test_expect_success 'but svn mkdirs recreates empty directories after but svn gc' '
	(
		cd cloned &&
		rm -r * &&
		but svn mkdirs &&
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
