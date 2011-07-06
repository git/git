#!/bin/sh
#
# Copyright (c) 2008 Brad King

test_description='git-svn dcommit honors auto-props'

. ./lib-git-svn.sh

generate_auto_props() {
cat << EOF
[miscellany]
enable-auto-props=$1
[auto-props]
*.sh  = svn:mime-type=application/x-shellscript; svn:eol-style=LF
*.txt = svn:mime-type=text/plain; svn:eol-style = native
EOF
}

test_expect_success 'initialize git-svn' '
	mkdir import &&
	(
		cd import &&
		echo foo >foo &&
		svn import -m "import for git-svn" . "$svnrepo"
	) &&
	rm -rf import &&
	git-svn init "$svnrepo"
	git-svn fetch
'

test_expect_success 'enable auto-props config' '
	cd "$gittestrepo" &&
	mkdir user &&
	generate_auto_props yes >user/config
'

test_expect_success 'add files matching auto-props' '
	cd "$gittestrepo" &&
	echo "#!$SHELL_PATH" >exec1.sh &&
	chmod +x exec1.sh &&
	echo "hello" >hello.txt &&
	echo bar >bar &&
	git add exec1.sh hello.txt bar &&
	git commit -m "files for enabled auto-props" &&
	git svn dcommit --config-dir=user
'

test_expect_success 'disable auto-props config' '
	cd "$gittestrepo" &&
	generate_auto_props no >user/config
'

test_expect_success 'add files matching disabled auto-props' '
	cd "$gittestrepo" &&
	echo "#$SHELL_PATH" >exec2.sh &&
	chmod +x exec2.sh &&
	echo "world" >world.txt &&
	echo zot >zot &&
	git add exec2.sh world.txt zot &&
	git commit -m "files for disabled auto-props" &&
	git svn dcommit --config-dir=user
'

test_expect_success 'check resulting svn repository' '
	mkdir work &&
	cd work &&
	svn co "$svnrepo" &&
	cd svnrepo &&

	# Check properties from first commit.
	test "x$(svn propget svn:executable exec1.sh)" = "x*" &&
	test "x$(svn propget svn:mime-type exec1.sh)" = \
	     "xapplication/x-shellscript" &&
	test "x$(svn propget svn:mime-type hello.txt)" = "xtext/plain" &&
	test "x$(svn propget svn:eol-style hello.txt)" = "xnative" &&
	test "x$(svn propget svn:mime-type bar)" = "x" &&

	# Check properties from second commit.
	test "x$(svn propget svn:executable exec2.sh)" = "x*" &&
	test "x$(svn propget svn:mime-type exec2.sh)" = "x" &&
	test "x$(svn propget svn:mime-type world.txt)" = "x" &&
	test "x$(svn propget svn:eol-style world.txt)" = "x" &&
	test "x$(svn propget svn:mime-type zot)" = "x"
'

test_done
