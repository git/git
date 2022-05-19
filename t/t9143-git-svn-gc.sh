#!/bin/sh
#
# Copyright (c) 2009 Robert Allan Zeh

test_description='but svn gc basic tests'

. ./lib-but-svn.sh

test_expect_success 'setup directories and test repo' '
	mkdir import &&
	mkdir tmp &&
	echo "Sample text for Subversion repository." > import/test.txt &&
	svn_cmd import -m "import for but svn" import "$svnrepo" > /dev/null
	'

test_expect_success 'checkout working copy from svn' \
	'svn_cmd co "$svnrepo" test_wc'

test_expect_success 'set some properties to create an unhandled.log file' '
	(
		cd test_wc &&
		svn_cmd propset foo bar test.txt &&
		svn_cmd cummit -m "property set"
	)'

test_expect_success 'Setup repo' 'but svn init "$svnrepo"'

test_expect_success 'Fetch repo' 'but svn fetch'

test_expect_success 'make backup copy of unhandled.log' '
	 cp .but/svn/refs/remotes/but-svn/unhandled.log tmp
	'

test_expect_success 'create leftover index' '> .but/svn/refs/remotes/but-svn/index'

test_expect_success 'but svn gc runs' 'but svn gc'

test_expect_success 'but svn index removed' '! test -f .but/svn/refs/remotes/but-svn/index'

if test -r .but/svn/refs/remotes/but-svn/unhandled.log.gz
then
	test_expect_success 'but svn gc produces a valid gzip file' '
		 gunzip .but/svn/refs/remotes/but-svn/unhandled.log.gz
		'
fi

test_expect_success 'but svn gc does not change unhandled.log files' '
	 test_cmp .but/svn/refs/remotes/but-svn/unhandled.log tmp/unhandled.log
	'

test_done
