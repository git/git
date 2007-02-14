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

echo try to try > expect.two
echo nothing to see here >> expect.two
cat expect.end >> expect.two

test_expect_success 'test left-hand-side only globbing' "
	git config --add svn-remote.two.url $svnrepo &&
	git config --add svn-remote.two.fetch trunk:refs/remotes/two/trunk &&
	git config --add svn-remote.two.branches \
	                 'branches/*:refs/remotes/two/branches/*' &&
	git config --add svn-remote.two.tags \
	                 'tags/*:refs/remotes/two/tags/*' &&
	cd tmp &&
		echo 'try try' >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		svn commit -m 'try to try'
		cd .. &&
	git-svn fetch two &&
	test \`git rev-list refs/remotes/two/tags/end | wc -l\` -eq 6 &&
	test \`git rev-list refs/remotes/two/branches/start | wc -l\` -eq 3 &&
	test \`git rev-parse refs/remotes/two/branches/start~2\` = \
	     \`git rev-parse refs/remotes/two/trunk\` &&
	test \`git rev-parse refs/remotes/two/tags/end~3\` = \
	     \`git rev-parse refs/remotes/two/branches/start\` &&
	git log --pretty=oneline refs/remotes/two/tags/end | \
	    sed -e 's/^.\{41\}//' > output.two &&
	cmp expect.two output.two
	"

test_done
