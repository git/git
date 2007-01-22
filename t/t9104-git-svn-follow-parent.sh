#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git-svn --follow-parent fetching'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' "
	mkdir import &&
	cd import &&
	mkdir -p trunk &&
	echo hello > trunk/readme &&
	svn import -m 'initial' . $svnrepo &&
	cd .. &&
	svn co $svnrepo wc &&
	cd wc &&
	echo world >> trunk/readme &&
	poke trunk/readme &&
	svn commit -m 'another commit' &&
	svn up &&
	svn mv -m 'rename to thunk' trunk thunk &&
	svn up &&
	echo goodbye >> thunk/readme &&
	poke thunk/readme &&
	svn commit -m 'bye now' &&
	cd ..
	"

test_expect_success 'init and fetch --follow-parent a moved directory' "
	git-svn init -i thunk $svnrepo/thunk &&
	git-svn fetch --follow-parent -i thunk &&
	test \"\`git-rev-parse --verify refs/remotes/trunk\`\" \
           = \"\`git-rev-parse --verify refs/remotes/thunk~1\`\" &&
        test \"\`git-cat-file blob refs/remotes/thunk:readme |\
                 sed -n -e '3p'\`\" = goodbye
	"

test_expect_success 'init and fetch from one svn-remote' "
        git-repo-config svn-remote.git-svn.url $svnrepo &&
        git-repo-config --add svn-remote.git-svn.fetch \
          trunk:refs/remotes/svn/trunk &&
        git-repo-config --add svn-remote.git-svn.fetch \
          thunk:refs/remotes/svn/thunk &&
        git-svn fetch --follow-parent -i svn/thunk &&
	test \"\`git-rev-parse --verify refs/remotes/svn/trunk\`\" \
           = \"\`git-rev-parse --verify refs/remotes/svn/thunk~1\`\" &&
        test \"\`git-cat-file blob refs/remotes/svn/thunk:readme |\
                 sed -n -e '3p'\`\" = goodbye
        "

test_debug 'gitk --all &'

test_done
