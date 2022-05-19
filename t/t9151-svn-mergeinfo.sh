#!/bin/sh
#
# Copyright (c) 2007, 2009 Sam Vilain
#

test_description='but-svn svn mergeinfo properties'

. ./lib-but-svn.sh

test_expect_success 'load svn dump' "
	svnadmin load -q '$rawsvnrepo' \
	  <'$TEST_DIRECTORY/t9151/svn-mergeinfo.dump' &&
	but svn init --minimize-url -R svnmerge \
	  --rewrite-root=http://svn.example.org \
	  -T trunk -b branches '$svnrepo' &&
	but svn fetch --all
"

test_expect_success 'all svn merges became but merge cummits' '
	but rev-list --all --no-merges --grep=Merge >unmarked &&
	test_must_be_empty unmarked
'

test_expect_success 'cherry picks did not become but merge cummits' '
	but rev-list --all --merges --grep=Cherry >bad-cherries &&
	test_must_be_empty bad-cherries
'

test_expect_success 'svn non-merge merge cummits did not become but merge cummits' '
	but rev-list --all --merges --grep=non-merge >bad-non-merges &&
	test_must_be_empty bad-non-merges
'

test_expect_success 'cummit made to merged branch is reachable from the merge' '
	before_cummit=$(but rev-list --all --grep="trunk cummit before merging trunk to b2") &&
	merge_cummit=$(but rev-list --all --grep="Merge trunk to b2") &&
	but rev-list -1 $before_cummit --not $merge_cummit >not-reachable &&
	test_must_be_empty not-reachable
'

test_expect_success 'merging two branches in one cummit is detected correctly' '
	f1_cummit=$(but rev-list --all --grep="make f1 branch from trunk") &&
	f2_cummit=$(but rev-list --all --grep="make f2 branch from trunk") &&
	merge_cummit=$(but rev-list --all --grep="Merge f1 and f2 to trunk") &&
	but rev-list -1 $f1_cummit $f2_cummit --not $merge_cummit >not-reachable &&
	test_must_be_empty not-reachable
'

test_expect_failure 'everything got merged in the end' '
	but rev-list --all --not main >unmerged &&
	test_must_be_empty unmerged
'

test_done
