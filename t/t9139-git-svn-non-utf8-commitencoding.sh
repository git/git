#!/bin/sh
#
# Copyright (c) 2009 Eric Wong

test_description='git svn refuses to dcommit non-UTF8 messages'

. ./lib-git-svn.sh

# ISO-2022-JP can pass for valid UTF-8, so skipping that in this test

for H in ISO8859-1 eucJP
do
	test_expect_success "$H setup" '
		mkdir $H &&
		svn_cmd import -m "$H test" $H "$svnrepo"/$H &&
		git svn clone "$svnrepo"/$H $H
	'
done

for H in ISO8859-1 eucJP
do
	test_expect_success "$H commit on git side" '
	(
		cd $H &&
		git config i18n.commitencoding $H &&
		git checkout -b t refs/remotes/git-svn &&
		echo $H >F &&
		git add F &&
		git commit -a -F "$TEST_DIRECTORY"/t3900/$H.txt &&
		E=$(git cat-file commit HEAD | sed -ne "s/^encoding //p") &&
		test "z$E" = "z$H"
	)
	'
done

for H in ISO8859-1 eucJP
do
	test_expect_success "$H dcommit to svn" '
	(
		cd $H &&
		git config --unset i18n.commitencoding &&
		test_must_fail git svn dcommit
	)
	'
done

test_done
