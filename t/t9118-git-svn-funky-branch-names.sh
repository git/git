#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='but svn funky branch names'
. ./lib-but-svn.sh

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
non_reflog=$(svn_cmd ls "$svnrepo/pr ject/branches" | grep not-a | sed 's/\///' | sed 's/@/%40/')

test_expect_success 'test clone with funky branch names' '
	but svn clone -s "$svnrepo/pr ject" project &&
	(
		cd project &&
		but rev-parse "refs/remotes/origin/fun%20plugin" &&
		but rev-parse "refs/remotes/origin/more%20fun%20plugin!" &&
		but rev-parse "refs/remotes/origin/$scary_ref" &&
		but rev-parse "refs/remotes/origin/%2Eleading_dot" &&
		if test_have_prereq !MINGW
		then
			but rev-parse "refs/remotes/origin/trailing_dot%2E"
		fi &&
		but rev-parse "refs/remotes/origin/trailing_dotlock%2Elock" &&
		but rev-parse "refs/remotes/origin/$non_reflog"
	)
	'

test_expect_success 'test dcummit to funky branch' "
	(
		cd project &&
		but reset --hard 'refs/remotes/origin/more%20fun%20plugin!' &&
		echo hello >> foo &&
		but cummit -m 'hello' -- foo &&
		but svn dcummit
	)
	"

test_expect_success 'test dcummit to scary branch' '
	(
		cd project &&
		but reset --hard "refs/remotes/origin/$scary_ref" &&
		echo urls are scary >> foo &&
		but cummit -m "eep" -- foo &&
		but svn dcummit
	)
	'

test_expect_success 'test dcummit to trailing_dotlock branch' '
	(
		cd project &&
		but reset --hard "refs/remotes/origin/trailing_dotlock%2Elock" &&
		echo who names branches like this anyway? >> foo &&
		but cummit -m "bar" -- foo &&
		but svn dcummit
	)
	'

test_done
