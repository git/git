#!/bin/sh
#
# Copyright (c) 2008 Eric Wong
#

test_description='git svn authors file tests'

. ./lib-git-svn.sh

cat > svn-authors <<EOF
aa = AAAAAAA AAAAAAA <aa@example.com>
bb = BBBBBBB BBBBBBB <bb@example.com>
EOF

test_expect_success 'setup svnrepo' '
	for i in aa bb cc dd
	do
		svn_cmd mkdir -m $i --username $i "$svnrepo"/$i || return 1
	done
	'

test_expect_success 'start import with incomplete authors file' '
	test_must_fail git svn clone --authors-file=svn-authors "$svnrepo" x
	'

test_expect_success 'imported 2 revisions successfully' '
	(
		cd x &&
		git rev-list refs/remotes/git-svn >actual &&
		test_line_count = 2 actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn >actual &&
		grep "^author BBBBBBB BBBBBBB <bb@example\.com> " actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~1 >actual &&
		grep "^author AAAAAAA AAAAAAA <aa@example\.com> " actual
	)
	'

cat >> svn-authors <<EOF
cc = CCCCCCC CCCCCCC <cc@example.com>
dd = DDDDDDD DDDDDDD <dd@example.com>
EOF

test_expect_success 'continues to import once authors have been added' '
	(
		cd x &&
		git svn fetch --authors-file=../svn-authors &&
		git rev-list refs/remotes/git-svn >actual &&
		test_line_count = 4 actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn >actual &&
		grep "^author DDDDDDD DDDDDDD <dd@example\.com> " actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~1 >actual &&
		grep "^author CCCCCCC CCCCCCC <cc@example\.com> " actual
	)
	'

test_expect_success 'authors-file against globs' '
	svn_cmd mkdir -m globs --username aa \
	  "$svnrepo"/aa/trunk "$svnrepo"/aa/branches "$svnrepo"/aa/tags &&
	git svn clone --authors-file=svn-authors -s "$svnrepo"/aa aa-work &&
	for i in bb ee cc
	do
		branch="aa/branches/$i" &&
		svn_cmd mkdir -m "$branch" --username $i "$svnrepo/$branch" || return 1
	done
	'

test_expect_success 'fetch fails on ee' '
	( cd aa-work && test_must_fail git svn fetch --authors-file=../svn-authors )
	'

tmp_config_get () {
	git config --file=.git/svn/.metadata --get "$1"
}

test_expect_success 'failure happened without negative side effects' '
	(
		cd aa-work &&
		test 6 -eq "$(tmp_config_get svn-remote.svn.branches-maxRev)" &&
		test 6 -eq "$(tmp_config_get svn-remote.svn.tags-maxRev)"
	)
	'

cat >> svn-authors <<EOF
ee = EEEEEEE EEEEEEE <ee@example.com>
EOF

test_expect_success 'fetch continues after authors-file is fixed' '
	(
		cd aa-work &&
		git svn fetch --authors-file=../svn-authors &&
		test 8 -eq "$(tmp_config_get svn-remote.svn.branches-maxRev)" &&
		test 8 -eq "$(tmp_config_get svn-remote.svn.tags-maxRev)"
	)
	'

test_expect_success !MINGW 'fresh clone with svn.authors-file in config' '
	(
		rm -r "$GIT_DIR" &&
		test x = x"$(git config svn.authorsfile)" &&
		test_config="$HOME"/.gitconfig &&
		sane_unset GIT_DIR &&
		git config --global \
		  svn.authorsfile "$HOME"/svn-authors &&
		test x"$HOME"/svn-authors = x"$(git config svn.authorsfile)" &&
		git svn clone "$svnrepo" gitconfig.clone &&
		cd gitconfig.clone &&
		git log >actual &&
		nr_ex=$(grep "^Author:.*example.com" actual | wc -l) &&
		git rev-list HEAD >actual &&
		nr_rev=$(wc -l <actual) &&
		test $nr_rev -eq $nr_ex
	)
'

cat >> svn-authors <<EOF
ff = FFFFFFF FFFFFFF <>
EOF

test_expect_success 'authors-file imported user without email' '
	svn_cmd mkdir -m aa/branches/ff --username ff "$svnrepo/aa/branches/ff" &&
	(
		cd aa-work &&
		git svn fetch --authors-file=../svn-authors &&
		git rev-list -1 --pretty=raw refs/remotes/origin/ff | \
		  grep "^author FFFFFFF FFFFFFF <> "
	)
	'

test_debug 'GIT_DIR=gitconfig.clone/.git git log'

test_done
