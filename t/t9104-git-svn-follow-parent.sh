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

test_expect_success 'follow deleted parent' "
        svn cp -m 'resurrecting trunk as junk' \
               -r2 $svnrepo/trunk $svnrepo/junk &&
        git-repo-config --add svn-remote.git-svn.fetch \
          junk:refs/remotes/svn/junk &&
        git-svn fetch --follow-parent -i svn/thunk &&
        git-svn fetch -i svn/junk --follow-parent &&
        test -z \"\`git diff svn/junk svn/trunk\`\" &&
        test \"\`git merge-base svn/junk svn/trunk\`\" \
           = \"\`git rev-parse svn/trunk\`\"
        "

test_expect_success 'follow larger parent' "
        mkdir -p import/trunk/thunk/bump/thud &&
        echo hi > import/trunk/thunk/bump/thud/file &&
        svn import -m 'import a larger parent' import $svnrepo/larger-parent &&
        svn cp -m 'hi' $svnrepo/larger-parent $svnrepo/another-larger &&
        git-svn init -i larger $svnrepo/another-larger/trunk/thunk/bump/thud &&
        git-svn fetch -i larger --follow-parent &&
        git-rev-parse --verify refs/remotes/larger &&
        git-rev-parse --verify \
           refs/remotes/larger-parent/trunk/thunk/bump/thud &&
        test \"\`git-merge-base \
                 refs/remotes/larger-parent/trunk/thunk/bump/thud \
                 refs/remotes/larger\`\" = \
             \"\`git-rev-parse refs/remotes/larger\`\"
        true
        "

test_debug 'gitk --all &'

test_done
