#!/bin/sh
#
# Copyright (c) 2008 Kevin Ballard
#

test_description='but svn clone with percent escapes'
. ./lib-but-svn.sh

test_expect_success 'setup svnrepo' '
	mkdir project project/trunk project/branches project/tags &&
	echo foo > project/trunk/foo &&
	svn_cmd import -m "$test_description" project "$svnrepo/pr ject" &&
	svn_cmd cp -m "branch" "$svnrepo/pr ject/trunk" \
	  "$svnrepo/pr ject/branches/b" &&
	svn_cmd cp -m "tag" "$svnrepo/pr ject/trunk" \
	  "$svnrepo/pr ject/tags/v1" &&
	rm -rf project &&
	maybe_start_httpd
'

test_expect_success 'test clone with percent escapes' '
	but svn clone "$svnrepo/pr%20ject" clone &&
	(
		cd clone &&
		but rev-parse refs/remotes/but-svn
	)
'

# SVN works either way, so should we...

test_expect_success 'svn checkout with percent escapes' '
	svn_cmd checkout "$svnrepo/pr%20ject" svn.percent &&
	svn_cmd checkout "$svnrepo/pr%20ject/trunk" svn.percent.trunk
'

test_expect_success 'svn checkout with space' '
	svn_cmd checkout "$svnrepo/pr ject" svn.space &&
	svn_cmd checkout "$svnrepo/pr ject/trunk" svn.space.trunk
'

test_expect_success 'test clone trunk with percent escapes and minimize-url' '
	but svn clone --minimize-url "$svnrepo/pr%20ject/trunk" minimize &&
	(
		cd minimize &&
		but rev-parse refs/remotes/but-svn
	)
'

test_expect_success 'test clone trunk with percent escapes' '
	but svn clone "$svnrepo/pr%20ject/trunk" trunk &&
	(
		cd trunk &&
		but rev-parse refs/remotes/but-svn
	)
'

test_expect_success 'test clone --stdlayout with percent escapes' '
	but svn clone --stdlayout "$svnrepo/pr%20ject" percent &&
	(
		cd percent &&
		but rev-parse refs/remotes/origin/trunk^0 &&
		but rev-parse refs/remotes/origin/b^0 &&
		but rev-parse refs/remotes/origin/tags/v1^0
	)
'

test_expect_success 'test clone -s with unescaped space' '
	but svn clone -s "$svnrepo/pr ject" --prefix origin/ space &&
	(
		cd space &&
		but rev-parse refs/remotes/origin/trunk^0 &&
		but rev-parse refs/remotes/origin/b^0 &&
		but rev-parse refs/remotes/origin/tags/v1^0
	)
'

test_done
