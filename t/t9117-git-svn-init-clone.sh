#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='but svn init/clone tests'

. ./lib-but-svn.sh

test_expect_success 'setup svnrepo' '
	mkdir project project/trunk project/branches project/tags &&
	echo foo > project/trunk/foo &&
	svn_cmd import -m "$test_description" project "$svnrepo"/project &&
	rm -rf project
	'

test_expect_success 'basic clone' '
	test ! -d trunk &&
	but svn clone "$svnrepo"/project/trunk &&
	test -d trunk/.but/svn &&
	test -e trunk/foo &&
	rm -rf trunk
	'

test_expect_success 'clone to target directory' '
	test ! -d target &&
	but svn clone "$svnrepo"/project/trunk target &&
	test -d target/.but/svn &&
	test -e target/foo &&
	rm -rf target
	'

test_expect_success 'clone with --stdlayout' '
	test ! -d project &&
	but svn clone -s "$svnrepo"/project &&
	test -d project/.but/svn &&
	test -e project/foo &&
	rm -rf project
	'

test_expect_success 'clone to target directory with --stdlayout' '
	test ! -d target &&
	but svn clone -s "$svnrepo"/project target &&
	test -d target/.but/svn &&
	test -e target/foo &&
	rm -rf target
	'

test_expect_success 'init without -s/-T/-b/-t does not warn' '
	test ! -d trunk &&
	but svn init "$svnrepo"/project/trunk trunk 2>warning &&
	! grep -q prefix warning &&
	rm -rf trunk &&
	rm -f warning
	'

test_expect_success 'clone without -s/-T/-b/-t does not warn' '
	test ! -d trunk &&
	but svn clone "$svnrepo"/project/trunk 2>warning &&
	! grep -q prefix warning &&
	rm -rf trunk &&
	rm -f warning
	'

test_svn_configured_prefix () {
	prefix=$1 &&
	cat >expect <<EOF &&
project/trunk:refs/remotes/${prefix}trunk
project/branches/*:refs/remotes/${prefix}*
project/tags/*:refs/remotes/${prefix}tags/*
EOF
	test ! -f actual &&
	but --but-dir=project/.but config svn-remote.svn.fetch >>actual &&
	but --but-dir=project/.but config svn-remote.svn.branches >>actual &&
	but --but-dir=project/.but config svn-remote.svn.tags >>actual &&
	test_cmp expect actual &&
	rm -f expect actual
}

test_expect_success 'init with -s/-T/-b/-t assumes --prefix=origin/' '
	test ! -d project &&
	but svn init -s "$svnrepo"/project project 2>warning &&
	! grep -q prefix warning &&
	test_svn_configured_prefix "origin/" &&
	rm -rf project &&
	rm -f warning
	'

test_expect_success 'clone with -s/-T/-b/-t assumes --prefix=origin/' '
	test ! -d project &&
	but svn clone -s "$svnrepo"/project 2>warning &&
	! grep -q prefix warning &&
	test_svn_configured_prefix "origin/" &&
	rm -rf project &&
	rm -f warning
	'

test_expect_success 'init with -s/-T/-b/-t and --prefix "" still works' '
	test ! -d project &&
	but svn init -s "$svnrepo"/project project --prefix "" 2>warning &&
	! grep -q prefix warning &&
	test_svn_configured_prefix "" &&
	rm -rf project &&
	rm -f warning
	'

test_expect_success 'clone with -s/-T/-b/-t and --prefix "" still works' '
	test ! -d project &&
	but svn clone -s "$svnrepo"/project --prefix "" 2>warning &&
	! grep -q prefix warning &&
	test_svn_configured_prefix "" &&
	rm -rf project &&
	rm -f warning
	'

test_expect_success 'init with -T as a full url works' '
	test ! -d project &&
	but svn init -T "$svnrepo"/project/trunk project &&
	rm -rf project
	'

test_done
