#!/bin/sh
# Copyright (c) 2007 Eric Wong
test_description='git-svn globbing refspecs'
. ./lib-git-svn.sh

cat > expect.end <<EOF
the end
hi
start a new branch
initial
EOF

test_expect_success 'test refspec globbing' "
	mkdir -p trunk/src/a trunk/src/b trunk/doc &&
	echo 'hello world' > trunk/src/a/readme &&
	echo 'goodbye world' > trunk/src/b/readme &&
	svn import -m 'initial' trunk $svnrepo/trunk &&
	svn co $svnrepo tmp &&
	cd tmp &&
		mkdir branches tags &&
		svn add branches tags &&
		svn cp trunk branches/start &&
		svn commit -m 'start a new branch' &&
		svn up &&
		echo 'hi' >> branches/start/src/b/readme &&
		poke branches/start/src/b/readme &&
		echo 'hey' >> branches/start/src/a/readme &&
		poke branches/start/src/a/readme &&
		svn commit -m 'hi' &&
		svn up &&
		svn cp branches/start tags/end &&
		echo 'bye' >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		echo 'aye' >> tags/end/src/a/readme &&
		poke tags/end/src/a/readme &&
		svn commit -m 'the end' &&
		echo 'byebye' >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		svn commit -m 'nothing to see here'
		cd .. &&
	git config --add svn-remote.svn.url $svnrepo &&
	git config --add svn-remote.svn.fetch \
	                 'trunk/src/a:refs/remotes/trunk' &&
	git config --add svn-remote.svn.branches \
	                 'branches/*/src/a:refs/remotes/branches/*' &&
	git config --add svn-remote.svn.tags\
	                 'tags/*/src/a:refs/remotes/tags/*' &&
	git-svn multi-fetch &&
	git log --pretty=oneline refs/remotes/tags/end | \
	    sed -e 's/^.\{41\}//' > output.end &&
	cmp expect.end output.end &&
	test \"\`git rev-parse refs/remotes/tags/end~1\`\" = \
		\"\`git rev-parse refs/remotes/branches/start\`\" &&
	test \"\`git rev-parse refs/remotes/branches/start~2\`\" = \
		\"\`git rev-parse refs/remotes/trunk\`\"
	"

test_done
