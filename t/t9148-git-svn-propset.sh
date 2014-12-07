#!/bin/sh
#
# Copyright (c) 2014 Alfred Perlstein
#

test_description='git svn propset tests'

. ./lib-git-svn.sh

foo_subdir2="subdir/subdir2/foo_subdir2"

set -e
mkdir import &&
(set -e ; cd import
	mkdir subdir
	mkdir subdir/subdir2
	touch foo 		# for 'add props top level'
	touch subdir/foo_subdir # for 'add props relative'
	touch "$foo_subdir2"	# for 'add props subdir'
	svn_cmd import -m 'import for git svn' . "$svnrepo" >/dev/null
)
rm -rf import

test_expect_success 'initialize git svn' '
	git svn init "$svnrepo"
	'

test_expect_success 'fetch revisions from svn' '
	git svn fetch
	'

set_props () {
	subdir="$1"
	file="$2"
	shift;shift;
	(cd "$subdir" &&
		while [ $# -gt 0 ] ; do
			git svn propset "$1" "$2" "$file" || exit 1
			shift;shift;
		done &&
		echo hello >> "$file" &&
		git commit -m "testing propset" "$file")
}

confirm_props () {
	subdir="$1"
	file="$2"
	shift;shift;
	(set -e ; cd "svn_project/$subdir" &&
		while [ $# -gt 0 ] ; do
			test "$(svn_cmd propget "$1" "$file")" = "$2" || exit 1
			shift;shift;
		done)
}


#The current implementation has a restriction:
#svn propset will be taken as a delta for svn dcommit only
#if the file content is also modified
test_expect_success 'add props top level' '
	set_props "." "foo" "svn:keywords" "FreeBSD=%H" &&
	git svn dcommit &&
	svn_cmd co "$svnrepo" svn_project &&
	confirm_props "." "foo" "svn:keywords" "FreeBSD=%H" &&
	rm -rf svn_project
	'

test_expect_success 'add multiple props' '
	set_props "." "foo" \
		"svn:keywords" "FreeBSD=%H" fbsd:nokeywords yes &&
	git svn dcommit &&
	svn_cmd co "$svnrepo" svn_project &&
	confirm_props "." "foo" \
		"svn:keywords" "FreeBSD=%H" fbsd:nokeywords yes &&
	rm -rf svn_project
	'

test_expect_success 'add props subdir' '
	set_props "." "$foo_subdir2" svn:keywords "FreeBSD=%H" &&
	git svn dcommit &&
	svn_cmd co "$svnrepo" svn_project &&
	confirm_props "." "$foo_subdir2" "svn:keywords" "FreeBSD=%H" &&
	rm -rf svn_project
	'

test_expect_success 'add props relative' '
	set_props "subdir/subdir2" "../foo_subdir" \
		svn:keywords "FreeBSD=%H" &&
	git svn dcommit &&
	svn_cmd co "$svnrepo" svn_project &&
	confirm_props "subdir/subdir2" "../foo_subdir" \
		svn:keywords "FreeBSD=%H" &&
	rm -rf svn_project
	'
test_done
