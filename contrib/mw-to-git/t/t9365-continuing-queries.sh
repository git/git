#!/bin/sh

test_description='Test the Git Mediawiki remote helper: queries w/ more than 500 results'

. ./test-gitmw-lib.sh
. $TEST_DIRECTORY/test-lib.sh

test_check_precond

test_expect_success 'creating page w/ >500 revisions' '
	wiki_reset &&
	for i in $(test_seq 501)
	do
		echo "creating revision $i" &&
		wiki_editpage foo "revision $i<br/>" true
	done
'

test_expect_success 'cloning page w/ >500 revisions' '
	git clone mediawiki::'"$WIKI_URL"' mw_dir
'

test_done
