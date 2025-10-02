#!/bin/sh

test_description='Test :/ object name notation'

. ./perf-lib.sh

test_perf_fresh_repo

#
# Creates lots of merges to make history traversal costly.  In
# particular it creates 2^($max_level-1)-1 2-way merges on top of
# 2^($max_level-1) root commits.  E.g., the commit history looks like
# this for a $max_level of 3:
#
#     _1_
#    /   \
#   2     3
#  / \   / \
# 4   5 6   7
#
# The numbers are the fast-import marks, which also are the commit
# messages.  1 is the HEAD commit and a merge, 2 and 3 are also merges,
# 4-7 are the root commits.
#
build_history () {
	local max_level="$1" &&
	local level="${2:-1}" &&
	local mark="${3:-1}" &&
	if test $level -eq $max_level
	then
		echo "reset refs/heads/master" &&
		echo "from $ZERO_OID" &&
		echo "commit refs/heads/master" &&
		echo "mark :$mark" &&
		echo "committer C <c@example.com> 1234567890 +0000" &&
		echo "data <<EOF" &&
		echo "$mark" &&
		echo "EOF"
	else
		local level1=$((level+1)) &&
		local mark1=$((2*mark)) &&
		local mark2=$((2*mark+1)) &&
		build_history $max_level $level1 $mark1 &&
		build_history $max_level $level1 $mark2 &&
		echo "commit refs/heads/master" &&
		echo "mark :$mark" &&
		echo "committer C <c@example.com> 1234567890 +0000" &&
		echo "data <<EOF" &&
		echo "$mark" &&
		echo "EOF" &&
		echo "from :$mark1" &&
		echo "merge :$mark2"
	fi
}

test_expect_success 'setup' '
	build_history 16 | git fast-import &&
	git log --format="%H %s" --reverse >commits &&
	sed -n -e "s/ .*$//p" -e "q" <commits >expect &&
	sed -n -e "s/^.* //p" -e "q" <commits >needle
'

test_perf "rev-parse :/$(cat needle)" '
	git rev-parse :/$(cat needle) >actual
'

test_expect_success 'verify result' '
	test_cmp expect actual
'

test_done
