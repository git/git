#!/bin/sh
#
# Copyright (c) 2007, 2009 Sam Vilain
#

test_description='git-svn svn mergeinfo properties'

. ./lib-git-svn.sh

test_expect_success 'load svn dump' "
	svnadmin load -q '$rawsvnrepo' \
	  < '$TEST_DIRECTORY/t9151/svn-mergeinfo.dump' &&
	git svn init --minimize-url -R svnmerge \
	  -T trunk -b branches '$svnrepo' &&
	git svn fetch --all
	"

test_expect_success 'all svn merges became git merge commits' '
	unmarked=$(git rev-list --parents --all --grep=Merge |
		grep -v " .* " | cut -f1 -d" ")
	[ -z "$unmarked" ]
	'

test_expect_success 'cherry picks did not become git merge commits' '
	bad_cherries=$(git rev-list --parents --all --grep=Cherry |
		grep " .* " | cut -f1 -d" ")
	[ -z "$bad_cherries" ]
	'

test_expect_success 'svn non-merge merge commits did not become git merge commits' '
	bad_non_merges=$(git rev-list --parents --all --grep=non-merge |
		grep " .* " | cut -f1 -d" ")
	[ -z "$bad_non_merges" ]
	'

test_expect_failure 'everything got merged in the end' '
	unmerged=$(git rev-list --all --not master)
	[ -z "$unmerged" ]
	'

test_done
