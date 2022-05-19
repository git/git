#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='but svn useSvmProps test'

. ./lib-but-svn.sh

test_expect_success 'load svm repo' '
	svnadmin load -q "$rawsvnrepo" < "$TEST_DIRECTORY"/t9110/svm.dump &&
	but svn init --minimize-url -R arr -i bar "$svnrepo"/mirror/arr &&
	but svn init --minimize-url -R argh -i dir "$svnrepo"/mirror/argh &&
	but svn init --minimize-url -R argh -i e \
	  "$svnrepo"/mirror/argh/a/b/c/d/e &&
	but config svn.useSvmProps true &&
	but svn fetch --all
	'

uuid=161ce429-a9dd-4828-af4a-52023f968c89

bar_url=http://mayonaise/svnrepo/bar
test_expect_success 'verify metadata for /bar' "
	but cat-file cummit refs/remotes/bar >actual &&
	grep '^but-svn-id: $bar_url@12 $uuid$' actual &&
	but cat-file cummit refs/remotes/bar~1 >actual &&
	grep '^but-svn-id: $bar_url@11 $uuid$' actual &&
	but cat-file cummit refs/remotes/bar~2 >actual &&
	grep '^but-svn-id: $bar_url@10 $uuid$' actual &&
	but cat-file cummit refs/remotes/bar~3 >actual &&
	grep '^but-svn-id: $bar_url@9 $uuid$' actual &&
	but cat-file cummit refs/remotes/bar~4 >actual &&
	grep '^but-svn-id: $bar_url@6 $uuid$' actual &&
	but cat-file cummit refs/remotes/bar~5 >actual &&
	grep '^but-svn-id: $bar_url@1 $uuid$' actual
	"

e_url=http://mayonaise/svnrepo/dir/a/b/c/d/e
test_expect_success 'verify metadata for /dir/a/b/c/d/e' "
	but cat-file cummit refs/remotes/e >actual &&
	grep '^but-svn-id: $e_url@1 $uuid$' actual
	"

dir_url=http://mayonaise/svnrepo/dir
test_expect_success 'verify metadata for /dir' "
	but cat-file cummit refs/remotes/dir >actual &&
	grep '^but-svn-id: $dir_url@2 $uuid$' actual &&
	but cat-file cummit refs/remotes/dir~1 >actual &&
	grep '^but-svn-id: $dir_url@1 $uuid$' actual
	"

test_expect_success 'find cummit based on SVN revision number' "
	but svn find-rev r12 >actual &&
	grep $(but rev-parse HEAD) actual
        "

test_expect_success 'empty rebase' "
	but svn rebase
	"

test_done
