#!/bin/sh
# Copyright (c) 2007 Eric Wong
test_description='git svn globbing refspecs'
. ./lib-git-svn.sh

cat > expect.end <<EOF
the end
hi
start a new branch
initial
EOF

test_expect_success 'test refspec globbing' '
	mkdir -p trunk/src/a trunk/src/b trunk/doc &&
	echo "hello world" > trunk/src/a/readme &&
	echo "goodbye world" > trunk/src/b/readme &&
	svn_cmd import -m "initial" trunk "$svnrepo"/trunk &&
	svn_cmd co "$svnrepo" tmp &&
	(
		cd tmp &&
		mkdir branches tags &&
		svn_cmd add branches tags &&
		svn_cmd cp trunk branches/start &&
		svn_cmd commit -m "start a new branch" &&
		svn_cmd up &&
		echo "hi" >> branches/start/src/b/readme &&
		poke branches/start/src/b/readme &&
		echo "hey" >> branches/start/src/a/readme &&
		poke branches/start/src/a/readme &&
		svn_cmd commit -m "hi" &&
		svn_cmd up &&
		svn_cmd cp branches/start tags/end &&
		echo "bye" >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		echo "aye" >> tags/end/src/a/readme &&
		poke tags/end/src/a/readme &&
		svn_cmd commit -m "the end" &&
		echo "byebye" >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		svn_cmd commit -m "nothing to see here"
	) &&
	git config --add svn-remote.svn.url "$svnrepo" &&
	git config --add svn-remote.svn.fetch \
	                 "trunk/src/a:refs/remotes/trunk" &&
	git config --add svn-remote.svn.branches \
	                 "branches/*/src/a:refs/remotes/branches/*" &&
	git config --add svn-remote.svn.tags\
	                 "tags/*/src/a:refs/remotes/tags/*" &&
	git svn multi-fetch &&
	git log --pretty=oneline refs/remotes/tags/end >actual &&
	sed -e "s/^.\{41\}//" actual >output.end &&
	test_cmp expect.end output.end &&
	test "$(git rev-parse refs/remotes/tags/end~1)" = \
		"$(git rev-parse refs/remotes/branches/start)" &&
	test "$(git rev-parse refs/remotes/branches/start~2)" = \
		"$(git rev-parse refs/remotes/trunk)" &&
	test_must_fail git rev-parse refs/remotes/tags/end@3
	'

echo try to try > expect.two
echo nothing to see here >> expect.two
cat expect.end >> expect.two

test_expect_success 'test left-hand-side only globbing' '
	git config --add svn-remote.two.url "$svnrepo" &&
	git config --add svn-remote.two.fetch trunk:refs/remotes/two/trunk &&
	git config --add svn-remote.two.branches \
	                 "branches/*:refs/remotes/two/branches/*" &&
	git config --add svn-remote.two.tags \
	                 "tags/*:refs/remotes/two/tags/*" &&
	(
		cd tmp &&
		echo "try try" >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		svn_cmd commit -m "try to try"
	) &&
	git svn fetch two &&
	git rev-list refs/remotes/two/tags/end >actual &&
	test_line_count = 6 actual &&
	git rev-list refs/remotes/two/branches/start >actual &&
	test_line_count = 3 actual &&
	test $(git rev-parse refs/remotes/two/branches/start~2) = \
	     $(git rev-parse refs/remotes/two/trunk) &&
	test $(git rev-parse refs/remotes/two/tags/end~3) = \
	     $(git rev-parse refs/remotes/two/branches/start) &&
	git log --pretty=oneline refs/remotes/two/tags/end >actual &&
	sed -e "s/^.\{41\}//" actual >output.two &&
	test_cmp expect.two output.two
	'

test_expect_success 'prepare test disallow multi-globs' "
cat >expect.three <<EOF
Only one set of wildcards (e.g. '*' or '*/*/*') is supported: branches/*/t/*

EOF
	"

test_expect_success 'test disallow multi-globs' '
	git config --add svn-remote.three.url "$svnrepo" &&
	git config --add svn-remote.three.fetch \
	                 trunk:refs/remotes/three/trunk &&
	git config --add svn-remote.three.branches \
	                 "branches/*/t/*:refs/remotes/three/branches/*" &&
	git config --add svn-remote.three.tags \
	                 "tags/*/*:refs/remotes/three/tags/*" &&
	(
		cd tmp &&
		echo "try try" >> tags/end/src/b/readme &&
		poke tags/end/src/b/readme &&
		svn_cmd commit -m "try to try"
	) &&
	test_must_fail git svn fetch three 2> stderr.three &&
	test_cmp expect.three stderr.three
	'

test_done
