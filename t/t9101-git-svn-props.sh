#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git-svn property tests'
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
	a_crlf=`git-hash-object -w crlf`
	printf "Hello\rWorld\r" > cr
	a_cr=`git-hash-object -w cr`
	printf "Hello\nWorld\n" > lf
	a_lf=`git-hash-object -w lf`

	printf "Hello\r\nWorld" > ne_crlf
	a_ne_crlf=`git-hash-object -w ne_crlf`
	printf "Hello\nWorld" > ne_lf
	a_ne_lf=`git-hash-object -w ne_lf`
	printf "Hello\rWorld" > ne_cr
	a_ne_cr=`git-hash-object -w ne_cr`

	touch empty
	a_empty=`git-hash-object -w empty`
	printf "\n" > empty_lf
	a_empty_lf=`git-hash-object -w empty_lf`
	printf "\r" > empty_cr
	a_empty_cr=`git-hash-object -w empty_cr`
	printf "\r\n" > empty_crlf
	a_empty_crlf=`git-hash-object -w empty_crlf`

	svn import -m 'import for git-svn' . "$svnrepo" >/dev/null
cd ..

rm -rf import
test_expect_success 'checkout working copy from svn' "svn co $svnrepo test_wc"
test_expect_success 'setup some commits to svn' \
	'cd test_wc &&
		echo Greetings >> kw.c &&
		poke kw.c &&
		svn commit -m "Not yet an Id" &&
		echo Hello world >> kw.c &&
		poke kw.c &&
		svn commit -m "Modified file, but still not yet an Id" &&
		svn propset svn:keywords Id kw.c &&
		poke kw.c &&
		svn commit -m "Propset Id" &&
	cd ..'

test_expect_success 'initialize git-svn' "git-svn init $svnrepo"
test_expect_success 'fetch revisions from svn' 'git-svn fetch'

name='test svn:keywords ignoring'
test_expect_success "$name" \
	'git checkout -b mybranch remotes/git-svn &&
	echo Hi again >> kw.c &&
	git commit -a -m "test keywords ignoring" &&
	git-svn set-tree remotes/git-svn..mybranch &&
	git pull . remotes/git-svn'

expect='/* $Id$ */'
got="`sed -ne 2p kw.c`"
test_expect_success 'raw $Id$ found in kw.c' "test '$expect' = '$got'"

test_expect_success "propset CR on crlf files" \
	'cd test_wc &&
		svn propset svn:eol-style CR empty &&
		svn propset svn:eol-style CR crlf &&
		svn propset svn:eol-style CR ne_crlf &&
		svn commit -m "propset CR on crlf files" &&
	 cd ..'

test_expect_success 'fetch and pull latest from svn and checkout a new wc' \
	"git-svn fetch &&
	 git pull . remotes/git-svn &&
	 svn co $svnrepo new_wc"

for i in crlf ne_crlf lf ne_lf cr ne_cr empty_cr empty_lf empty empty_crlf
do
	test_expect_success "Comparing $i" "cmp $i new_wc/$i"
done


cd test_wc
	printf '$Id$\rHello\rWorld\r' > cr
	printf '$Id$\rHello\rWorld' > ne_cr
	a_cr=`printf '$Id$\r\nHello\r\nWorld\r\n' | git-hash-object --stdin`
	a_ne_cr=`printf '$Id$\r\nHello\r\nWorld' | git-hash-object --stdin`
	test_expect_success 'Set CRLF on cr files' \
	'svn propset svn:eol-style CRLF cr &&
	 svn propset svn:eol-style CRLF ne_cr &&
	 svn propset svn:keywords Id cr &&
	 svn propset svn:keywords Id ne_cr &&
	 svn commit -m "propset CRLF on cr files"'
cd ..
test_expect_success 'fetch and pull latest from svn' \
	'git-svn fetch && git pull . remotes/git-svn'

b_cr="`git-hash-object cr`"
b_ne_cr="`git-hash-object ne_cr`"

test_expect_success 'CRLF + $Id$' "test '$a_cr' = '$b_cr'"
test_expect_success 'CRLF + $Id$ (no newline)' "test '$a_ne_cr' = '$b_ne_cr'"

cat > show-ignore.expect <<\EOF

# /
/no-such-file*

# deeply
/deeply/no-such-file*

# deeply/nested
/deeply/nested/no-such-file*

# deeply/nested/directory
/deeply/nested/directory/no-such-file*
EOF

test_expect_success 'test show-ignore' "
	cd test_wc &&
	mkdir -p deeply/nested/directory &&
	svn add deeply &&
	svn up &&
	svn propset -R svn:ignore 'no-such-file*' .
	svn commit -m 'propset svn:ignore'
	cd .. &&
	git-svn show-ignore > show-ignore.got &&
	cmp show-ignore.expect show-ignore.got
	"

test_done
