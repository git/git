#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git-svn fetching'
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

test_expect_success 'init and fetch a moved directory' "
	git-svn init --minimize-url -i thunk $svnrepo/thunk &&
	git-svn fetch -i thunk &&
	test \"\`git-rev-parse --verify refs/remotes/thunk@2\`\" \
           = \"\`git-rev-parse --verify refs/remotes/thunk~1\`\" &&
        test \"\`git-cat-file blob refs/remotes/thunk:readme |\
                 sed -n -e '3p'\`\" = goodbye &&
	test -z \"\`git-config --get svn-remote.svn.fetch \
	         '^trunk:refs/remotes/thunk@2$'\`\"
	"

test_expect_success 'init and fetch from one svn-remote' "
        git-config svn-remote.svn.url $svnrepo &&
        git-config --add svn-remote.svn.fetch \
          trunk:refs/remotes/svn/trunk &&
        git-config --add svn-remote.svn.fetch \
          thunk:refs/remotes/svn/thunk &&
        git-svn fetch -i svn/thunk &&
	test \"\`git-rev-parse --verify refs/remotes/svn/trunk\`\" \
           = \"\`git-rev-parse --verify refs/remotes/svn/thunk~1\`\" &&
        test \"\`git-cat-file blob refs/remotes/svn/thunk:readme |\
                 sed -n -e '3p'\`\" = goodbye
        "

test_expect_success 'follow deleted parent' "
        svn cp -m 'resurrecting trunk as junk' \
               -r2 $svnrepo/trunk $svnrepo/junk &&
        git-config --add svn-remote.svn.fetch \
          junk:refs/remotes/svn/junk &&
        git-svn fetch -i svn/thunk &&
        git-svn fetch -i svn/junk &&
        test -z \"\`git diff svn/junk svn/trunk\`\" &&
        test \"\`git merge-base svn/junk svn/trunk\`\" \
           = \"\`git rev-parse svn/trunk\`\"
        "

test_expect_success 'follow larger parent' "
        mkdir -p import/trunk/thunk/bump/thud &&
        echo hi > import/trunk/thunk/bump/thud/file &&
        svn import -m 'import a larger parent' import $svnrepo/larger-parent &&
        svn cp -m 'hi' $svnrepo/larger-parent $svnrepo/another-larger &&
        git-svn init --minimize-url -i larger \
          $svnrepo/another-larger/trunk/thunk/bump/thud &&
        git-svn fetch -i larger &&
        git-rev-parse --verify refs/remotes/larger &&
        git-rev-parse --verify \
           refs/remotes/larger-parent/trunk/thunk/bump/thud &&
        test \"\`git-merge-base \
                 refs/remotes/larger-parent/trunk/thunk/bump/thud \
                 refs/remotes/larger\`\" = \
             \"\`git-rev-parse refs/remotes/larger\`\"
        true
        "

test_expect_success 'follow higher-level parent' "
        svn mkdir -m 'follow higher-level parent' $svnrepo/blob &&
        svn co $svnrepo/blob blob &&
        cd blob &&
                echo hi > hi &&
                svn add hi &&
                svn commit -m 'hihi' &&
                cd ..
        svn mkdir -m 'new glob at top level' $svnrepo/glob &&
        svn mv -m 'move blob down a level' $svnrepo/blob $svnrepo/glob/blob &&
        git-svn init --minimize-url -i blob $svnrepo/glob/blob &&
        git-svn fetch -i blob
        "

test_expect_success 'follow deleted directory' "
	svn mv -m 'bye!' $svnrepo/glob/blob/hi $svnrepo/glob/blob/bye &&
	svn rm -m 'remove glob' $svnrepo/glob &&
	git-svn init --minimize-url -i glob $svnrepo/glob &&
	git-svn fetch -i glob &&
	test \"\`git cat-file blob refs/remotes/glob:blob/bye\`\" = hi &&
	test \"\`git ls-tree refs/remotes/glob | wc -l \`\" -eq 1
	"

# ref: r9270 of the Subversion repository: (http://svn.collab.net/repos/svn)
# in trunk/subversion/bindings/swig/perl
test_expect_success 'follow-parent avoids deleting relevant info' "
	mkdir -p import/trunk/subversion/bindings/swig/perl/t &&
	for i in a b c ; do \
	  echo \$i > import/trunk/subversion/bindings/swig/perl/\$i.pm &&
	  echo _\$i > import/trunk/subversion/bindings/swig/perl/t/\$i.t; \
	done &&
	  echo 'bad delete test' > \
	   import/trunk/subversion/bindings/swig/perl/t/larger-parent &&
	  echo 'bad delete test 2' > \
	   import/trunk/subversion/bindings/swig/perl/another-larger &&
	cd import &&
	  svn import -m 'r9270 test' . $svnrepo/r9270 &&
	cd .. &&
	svn co $svnrepo/r9270/trunk/subversion/bindings/swig/perl r9270 &&
	cd r9270 &&
	  svn mkdir native &&
	  svn mv t native/t &&
	  for i in a b c; do svn mv \$i.pm native/\$i.pm; done &&
	  echo z >> native/t/c.t &&
	  poke native/t/c.t &&
	  svn commit -m 'reorg test' &&
	cd .. &&
	git-svn init --minimize-url -i r9270-t \
	  $svnrepo/r9270/trunk/subversion/bindings/swig/perl/native/t &&
	git-svn fetch -i r9270-t &&
	test \`git rev-list r9270-t | wc -l\` -eq 2 &&
	test \"\`git ls-tree --name-only r9270-t~1\`\" = \
	     \"\`git ls-tree --name-only r9270-t\`\"
	"

test_expect_success "track initial change if it was only made to parent" "
	svn cp -m 'wheee!' $svnrepo/r9270/trunk $svnrepo/r9270/drunk &&
	git-svn init --minimize-url -i r9270-d \
	  $svnrepo/r9270/drunk/subversion/bindings/swig/perl/native/t &&
	git-svn fetch -i r9270-d &&
	test \`git rev-list r9270-d | wc -l\` -eq 3 &&
	test \"\`git ls-tree --name-only r9270-t\`\" = \
	     \"\`git ls-tree --name-only r9270-d\`\" &&
	test \"\`git rev-parse r9270-t\`\" = \
	     \"\`git rev-parse r9270-d~1\`\"
	"

test_expect_success "track multi-parent paths" "
	svn cp -m 'resurrect /glob' $svnrepo/r9270 $svnrepo/glob &&
	git-svn multi-fetch &&
	test \`git cat-file commit refs/remotes/glob | \
	       grep '^parent ' | wc -l\` -eq 2
	"

test_expect_success "multi-fetch continues to work" "
	git-svn multi-fetch
	"

test_expect_success "multi-fetch works off a 'clean' repository" "
	rm -r $GIT_DIR/svn $GIT_DIR/refs/remotes $GIT_DIR/logs &&
	mkdir $GIT_DIR/svn &&
	git-svn multi-fetch
	"

test_debug 'gitk --all &'

test_done
