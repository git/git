#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='git svn funky branch names'
. ./lib-git-svn.sh

# Abo-Uebernahme (Bug #994)
scary_uri='Abo-Uebernahme%20%28Bug%20%23994%29'
scary_ref='Abo-Uebernahme%20(Bug%20#994)'

test_expect_success 'setup svnrepo' '
	mkdir project project/trunk project/branches project/tags &&
	echo foo > project/trunk/foo &&
	svn_cmd import -m "$test_description" project "$svnrepo/pr ject" &&
	rm -rf project &&
	svn_cmd cp -m "fun" "$svnrepo/pr ject/trunk" \
	                "$svnrepo/pr ject/branches/fun plugin" &&
	svn_cmd cp -m "more fun!" "$svnrepo/pr ject/branches/fun plugin" \
	                      "$svnrepo/pr ject/branches/more fun plugin!" &&
	svn_cmd cp -m "scary" "$svnrepo/pr ject/branches/fun plugin" \
	              "$svnrepo/pr ject/branches/$scary_uri" &&
	svn_cmd cp -m "leading dot" "$svnrepo/pr ject/trunk" \
			"$svnrepo/pr ject/branches/.leading_dot" &&
	if test_have_prereq !MINGW
	then
		svn_cmd cp -m "trailing dot" "$svnrepo/pr ject/trunk" \
			"$svnrepo/pr ject/branches/trailing_dot."
	fi &&
	svn_cmd cp -m "trailing .lock" "$svnrepo/pr ject/trunk" \
			"$svnrepo/pr ject/branches/trailing_dotlock.lock" &&
	svn_cmd cp -m "reflog" "$svnrepo/pr ject/trunk" \
			"$svnrepo/pr ject/branches/not-a@{0}reflog@" &&
	maybe_start_httpd
	'

# SVN 1.7 will truncate "not-a%40{0]" to just "not-a".
# Look at what SVN wound up naming the branch and use that.
# Be sure to escape the @ if it shows up.
non_reflog=$(svn_cmd ls "$svnrepo/pr ject/branches" | sed -ne '/not-a/ { s/\///; s/@/%40/; p; }')

test_expect_success 'test clone with funky branch names' '
	git svn clone -s "$svnrepo/pr ject" project &&
	(
		cd project &&
		git rev-parse "refs/remotes/origin/fun%20plugin" &&
		git rev-parse "refs/remotes/origin/more%20fun%20plugin!" &&
		git rev-parse "refs/remotes/origin/$scary_ref" &&
		git rev-parse "refs/remotes/origin/%2Eleading_dot" &&
		if test_have_prereq !MINGW
		then
			git rev-parse "refs/remotes/origin/trailing_dot%2E"
		fi &&
		git rev-parse "refs/remotes/origin/trailing_dotlock%2Elock" &&
		git rev-parse "refs/remotes/origin/$non_reflog"
	)
	'

test_expect_success 'test dcommit to funky branch' "
	(
		cd project &&
		git reset --hard 'refs/remotes/origin/more%20fun%20plugin!' &&
		echo hello >> foo &&
		git commit -m 'hello' -- foo &&
		git svn dcommit
	)
	"

test_expect_success 'test dcommit to scary branch' '
	(
		cd project &&
		git reset --hard "refs/remotes/origin/$scary_ref" &&
		echo urls are scary >> foo &&
		git commit -m "eep" -- foo &&
		git svn dcommit
	)
	'

test_expect_success 'test dcommit to trailing_dotlock branch' '
	(
		cd project &&
		git reset --hard "refs/remotes/origin/trailing_dotlock%2Elock" &&
		echo who names branches like this anyway? >> foo &&
		git commit -m "bar" -- foo &&
		git svn dcommit
	)
	'

test_done
