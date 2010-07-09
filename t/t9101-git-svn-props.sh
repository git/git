#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git svn property tests'
. ./lib-git-svn.sh

mkdir import

a_crlf=
a_lf=
a_cr=
a_ne_crlf=
a_ne_lf=
a_ne_cr=
a_empty=
a_empty_lf=
a_empty_cr=
a_empty_crlf=

cd import
	cat >> kw.c <<\EOF
/* Somebody prematurely put a keyword into this file */
/* $Id$ */
EOF

	printf "Hello\r\nWorld\r\n" > crlf
	a_crlf=`git hash-object -w crlf`
	printf "Hello\rWorld\r" > cr
	a_cr=`git hash-object -w cr`
	printf "Hello\nWorld\n" > lf
	a_lf=`git hash-object -w lf`

	printf "Hello\r\nWorld" > ne_crlf
	a_ne_crlf=`git hash-object -w ne_crlf`
	printf "Hello\nWorld" > ne_lf
	a_ne_lf=`git hash-object -w ne_lf`
	printf "Hello\rWorld" > ne_cr
	a_ne_cr=`git hash-object -w ne_cr`

	touch empty
	a_empty=`git hash-object -w empty`
	printf "\n" > empty_lf
	a_empty_lf=`git hash-object -w empty_lf`
	printf "\r" > empty_cr
	a_empty_cr=`git hash-object -w empty_cr`
	printf "\r\n" > empty_crlf
	a_empty_crlf=`git hash-object -w empty_crlf`

	svn_cmd import --no-auto-props -m 'import for git svn' . "$svnrepo" >/dev/null
cd ..

rm -rf import
test_expect_success 'checkout working copy from svn' 'svn co "$svnrepo" test_wc'
test_expect_success 'setup some commits to svn' \
	'cd test_wc &&
		echo Greetings >> kw.c &&
		poke kw.c &&
		svn_cmd commit -m "Not yet an Id" &&
		echo Hello world >> kw.c &&
		poke kw.c &&
		svn_cmd commit -m "Modified file, but still not yet an Id" &&
		svn_cmd propset svn:keywords Id kw.c &&
		poke kw.c &&
		svn_cmd commit -m "Propset Id" &&
	cd ..'

test_expect_success 'initialize git svn' 'git svn init "$svnrepo"'
test_expect_success 'fetch revisions from svn' 'git svn fetch'

name='test svn:keywords ignoring'
test_expect_success "$name" \
	'git checkout -b mybranch ${remotes_git_svn} &&
	echo Hi again >> kw.c &&
	git commit -a -m "test keywords ignoring" &&
	git svn set-tree ${remotes_git_svn}..mybranch &&
	git pull . ${remotes_git_svn}'

expect='/* $Id$ */'
got="`sed -ne 2p kw.c`"
test_expect_success 'raw $Id$ found in kw.c' "test '$expect' = '$got'"

test_expect_success "propset CR on crlf files" \
	'cd test_wc &&
		svn_cmd propset svn:eol-style CR empty &&
		svn_cmd propset svn:eol-style CR crlf &&
		svn_cmd propset svn:eol-style CR ne_crlf &&
		svn_cmd commit -m "propset CR on crlf files" &&
	 cd ..'

test_expect_success 'fetch and pull latest from svn and checkout a new wc' \
	'git svn fetch &&
	 git pull . ${remotes_git_svn} &&
	 svn_cmd co "$svnrepo" new_wc'

for i in crlf ne_crlf lf ne_lf cr ne_cr empty_cr empty_lf empty empty_crlf
do
	test_expect_success "Comparing $i" "cmp $i new_wc/$i"
done


cd test_wc
	printf '$Id$\rHello\rWorld\r' > cr
	printf '$Id$\rHello\rWorld' > ne_cr
	a_cr=`printf '$Id$\r\nHello\r\nWorld\r\n' | git hash-object --stdin`
	a_ne_cr=`printf '$Id$\r\nHello\r\nWorld' | git hash-object --stdin`
	test_expect_success 'Set CRLF on cr files' \
	'svn_cmd propset svn:eol-style CRLF cr &&
	 svn_cmd propset svn:eol-style CRLF ne_cr &&
	 svn_cmd propset svn:keywords Id cr &&
	 svn_cmd propset svn:keywords Id ne_cr &&
	 svn_cmd commit -m "propset CRLF on cr files"'
cd ..
test_expect_success 'fetch and pull latest from svn' \
	'git svn fetch && git pull . ${remotes_git_svn}'

b_cr="`git hash-object cr`"
b_ne_cr="`git hash-object ne_cr`"

test_expect_success 'CRLF + $Id$' "test '$a_cr' = '$b_cr'"
test_expect_success 'CRLF + $Id$ (no newline)' "test '$a_ne_cr' = '$b_ne_cr'"

cat > show-ignore.expect <<\EOF

# /
/no-such-file*

# /deeply/
/deeply/no-such-file*

# /deeply/nested/
/deeply/nested/no-such-file*

# /deeply/nested/directory/
/deeply/nested/directory/no-such-file*
EOF

test_expect_success 'test show-ignore' "
	cd test_wc &&
	mkdir -p deeply/nested/directory &&
	touch deeply/nested/directory/.keep &&
	svn_cmd add deeply &&
	svn_cmd up &&
	svn_cmd propset -R svn:ignore '
no-such-file*
' .
	svn_cmd commit -m 'propset svn:ignore'
	cd .. &&
	git svn show-ignore > show-ignore.got &&
	cmp show-ignore.expect show-ignore.got
	"

cat >create-ignore.expect <<\EOF
/no-such-file*
EOF

cat >create-ignore-index.expect <<\EOF
100644 8c52e5dfcd0a8b6b6bcfe6b41b89bcbf493718a5 0	.gitignore
100644 8c52e5dfcd0a8b6b6bcfe6b41b89bcbf493718a5 0	deeply/.gitignore
100644 8c52e5dfcd0a8b6b6bcfe6b41b89bcbf493718a5 0	deeply/nested/.gitignore
100644 8c52e5dfcd0a8b6b6bcfe6b41b89bcbf493718a5 0	deeply/nested/directory/.gitignore
EOF

test_expect_success 'test create-ignore' "
	git svn fetch && git pull . ${remotes_git_svn} &&
	git svn create-ignore &&
	cmp ./.gitignore create-ignore.expect &&
	cmp ./deeply/.gitignore create-ignore.expect &&
	cmp ./deeply/nested/.gitignore create-ignore.expect &&
	cmp ./deeply/nested/directory/.gitignore create-ignore.expect &&
	git ls-files -s | grep gitignore | cmp - create-ignore-index.expect
	"

cat >prop.expect <<\EOF

no-such-file*

EOF
cat >prop2.expect <<\EOF
8
EOF

# This test can be improved: since all the svn:ignore contain the same
# pattern, it can pass even though the propget did not execute on the
# right directory.
test_expect_success 'test propget' "
	git svn propget svn:ignore . | cmp - prop.expect &&
	cd deeply &&
	git svn propget svn:ignore . | cmp - ../prop.expect &&
	git svn propget svn:entry:committed-rev nested/directory/.keep \
	  | cmp - ../prop2.expect &&
	git svn propget svn:ignore .. | cmp - ../prop.expect &&
	git svn propget svn:ignore nested/ | cmp - ../prop.expect &&
	git svn propget svn:ignore ./nested | cmp - ../prop.expect &&
	git svn propget svn:ignore .././deeply/nested | cmp - ../prop.expect
	"

cat >prop.expect <<\EOF
Properties on '.':
  svn:entry:committed-date
  svn:entry:committed-rev
  svn:entry:last-author
  svn:entry:uuid
  svn:ignore
EOF
cat >prop2.expect <<\EOF
Properties on 'nested/directory/.keep':
  svn:entry:committed-date
  svn:entry:committed-rev
  svn:entry:last-author
  svn:entry:uuid
EOF

test_expect_success 'test proplist' "
	git svn proplist . | cmp - prop.expect &&
	git svn proplist nested/directory/.keep | cmp - prop2.expect
	"

test_done
