#!/bin/sh
test_description='git svn globbing refspecs with prefixed globs'
. ./lib-git-svn.sh

test_expect_success 'prepare test refspec prefixed globbing' '
	cat >expect.end <<EOF
the end
hi
start a new branch
initial
EOF
	'

test_expect_success 'test refspec prefixed globbing' '
	mkdir -p trunk/src/a trunk/src/b trunk/doc &&
	echo "hello world" >trunk/src/a/readme &&
	echo "goodbye world" >trunk/src/b/readme &&
	svn_cmd import -m "initial" trunk "$svnrepo"/trunk &&
	svn_cmd co "$svnrepo" tmp &&
	(
		cd tmp &&
		mkdir branches tags &&
		svn_cmd add branches tags &&
		svn_cmd cp trunk branches/b_start &&
		svn_cmd commit -m "start a new branch" &&
		svn_cmd up &&
		echo "hi" >>branches/b_start/src/b/readme &&
		poke branches/b_start/src/b/readme &&
		echo "hey" >>branches/b_start/src/a/readme &&
		poke branches/b_start/src/a/readme &&
		svn_cmd commit -m "hi" &&
		svn_cmd up &&
		svn_cmd cp branches/b_start tags/t_end &&
		echo "bye" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		echo "aye" >>tags/t_end/src/a/readme &&
		poke tags/t_end/src/a/readme &&
		svn_cmd commit -m "the end" &&
		echo "byebye" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		svn_cmd commit -m "nothing to see here"
	) &&
	git config --add svn-remote.svn.url "$svnrepo" &&
	git config --add svn-remote.svn.fetch \
			 "trunk/src/a:refs/remotes/trunk" &&
	git config --add svn-remote.svn.branches \
			 "branches/b_*/src/a:refs/remotes/branches/b_*" &&
	git config --add svn-remote.svn.tags\
			 "tags/t_*/src/a:refs/remotes/tags/t_*" &&
	git svn multi-fetch &&
	git log --pretty=oneline refs/remotes/tags/t_end | \
	    sed -e "s/^.\{41\}//" >output.end &&
	test_cmp expect.end output.end &&
	test "$(git rev-parse refs/remotes/tags/t_end~1)" = \
		"$(git rev-parse refs/remotes/branches/b_start)" &&
	test "$(git rev-parse refs/remotes/branches/b_start~2)" = \
		"$(git rev-parse refs/remotes/trunk)" &&
	test_must_fail git rev-parse refs/remotes/tags/t_end@3
	'

test_expect_success 'prepare test left-hand-side only prefixed globbing' '
	echo try to try >expect.two &&
	echo nothing to see here >>expect.two &&
	cat expect.end >>expect.two
	'

test_expect_success 'test left-hand-side only prefixed globbing' '
	git config --add svn-remote.two.url "$svnrepo" &&
	git config --add svn-remote.two.fetch trunk:refs/remotes/two/trunk &&
	git config --add svn-remote.two.branches \
			 "branches/b_*:refs/remotes/two/branches/*" &&
	git config --add svn-remote.two.tags \
			 "tags/t_*:refs/remotes/two/tags/*" &&
	(
		cd tmp &&
		echo "try try" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		svn_cmd commit -m "try to try"
	) &&
	git svn fetch two &&
	test $(git rev-list refs/remotes/two/tags/t_end | wc -l) -eq 6 &&
	test $(git rev-list refs/remotes/two/branches/b_start | wc -l) -eq 3 &&
	test $(git rev-parse refs/remotes/two/branches/b_start~2) = \
	     $(git rev-parse refs/remotes/two/trunk) &&
	test $(git rev-parse refs/remotes/two/tags/t_end~3) = \
	     $(git rev-parse refs/remotes/two/branches/b_start) &&
	git log --pretty=oneline refs/remotes/two/tags/t_end | \
	    sed -e "s/^.\{41\}//" >output.two &&
	test_cmp expect.two output.two
	'

test_expect_success 'prepare test prefixed globs match just prefix' '
	cat >expect.three <<EOF
Tag commit to t_
Branch commit to b_
initial
EOF
	'

test_expect_success 'test prefixed globs match just prefix' '
	git config --add svn-remote.three.url "$svnrepo" &&
	git config --add svn-remote.three.fetch \
			 trunk:refs/remotes/three/trunk &&
	git config --add svn-remote.three.branches \
			 "branches/b_*:refs/remotes/three/branches/*" &&
	git config --add svn-remote.three.tags \
			 "tags/t_*:refs/remotes/three/tags/*" &&
	(
		cd tmp &&
		svn_cmd cp trunk branches/b_ &&
		echo "Branch commit to b_" >>branches/b_/src/a/readme &&
		poke branches/b_/src/a/readme &&
		svn_cmd commit -m "Branch commit to b_" &&
		svn_cmd up && svn_cmd cp branches/b_ tags/t_ &&
		echo "Tag commit to t_" >>tags/t_/src/a/readme &&
		poke tags/t_/src/a/readme &&
		svn_cmd commit -m "Tag commit to t_" &&
		svn_cmd up
	) &&
	git svn fetch three &&
	test $(git rev-list refs/remotes/three/branches/b_ | wc -l) -eq 2 &&
	test $(git rev-list refs/remotes/three/tags/t_ | wc -l) -eq 3 &&
	test $(git rev-parse refs/remotes/three/branches/b_~1) = \
	     $(git rev-parse refs/remotes/three/trunk) &&
	test $(git rev-parse refs/remotes/three/tags/t_~1) = \
	     $(git rev-parse refs/remotes/three/branches/b_) &&
	git log --pretty=oneline refs/remotes/three/tags/t_ | \
	    sed -e "s/^.\{41\}//" >output.three &&
	test_cmp expect.three output.three
	'

test_expect_success 'prepare test disallow prefixed multi-globs' "
cat >expect.four <<EOF
Only one set of wildcards (e.g. '*' or '*/*/*') is supported: branches/b_*/t/*

EOF
	"

test_expect_success 'test disallow prefixed multi-globs' '
	git config --add svn-remote.four.url "$svnrepo" &&
	git config --add svn-remote.four.fetch \
			 trunk:refs/remotes/four/trunk &&
	git config --add svn-remote.four.branches \
			 "branches/b_*/t/*:refs/remotes/four/branches/*" &&
	git config --add svn-remote.four.tags \
			 "tags/t_*/*:refs/remotes/four/tags/*" &&
	(
		cd tmp &&
		echo "try try" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		svn_cmd commit -m "try to try"
	) &&
	test_must_fail git svn fetch four 2>stderr.four &&
	test_cmp expect.four stderr.four &&
	git config --unset svn-remote.four.branches &&
	git config --unset svn-remote.four.tags
	'

test_expect_success 'prepare test globbing in the middle of the word' '
	cat >expect.five <<EOF
Tag commit to fghij
Branch commit to abcde
initial
EOF
	'

test_expect_success 'test globbing in the middle of the word' '
	git config --add svn-remote.five.url "$svnrepo" &&
	git config --add svn-remote.five.fetch \
			 trunk:refs/remotes/five/trunk &&
	git config --add svn-remote.five.branches \
			 "branches/a*e:refs/remotes/five/branches/*" &&
	git config --add svn-remote.five.tags \
			 "tags/f*j:refs/remotes/five/tags/*" &&
	(
		cd tmp &&
		svn_cmd cp trunk branches/abcde &&
		echo "Branch commit to abcde" >>branches/abcde/src/a/readme &&
		poke branches/b_/src/a/readme &&
		svn_cmd commit -m "Branch commit to abcde" &&
		svn_cmd up &&
		svn_cmd cp branches/abcde tags/fghij &&
		echo "Tag commit to fghij" >>tags/fghij/src/a/readme &&
		poke tags/fghij/src/a/readme &&
		svn_cmd commit -m "Tag commit to fghij" &&
		svn_cmd up
	) &&
	git svn fetch five &&
	test $(git rev-list refs/remotes/five/branches/abcde | wc -l) -eq 2 &&
	test $(git rev-list refs/remotes/five/tags/fghij | wc -l) -eq 3 &&
	test $(git rev-parse refs/remotes/five/branches/abcde~1) = \
	     $(git rev-parse refs/remotes/five/trunk) &&
	test $(git rev-parse refs/remotes/five/tags/fghij~1) = \
	     $(git rev-parse refs/remotes/five/branches/abcde) &&
	git log --pretty=oneline refs/remotes/five/tags/fghij | \
	    sed -e "s/^.\{41\}//" >output.five &&
	test_cmp expect.five output.five
	'

test_expect_success 'prepare test disallow multiple asterisks in one word' "
	echo \"Only one '*' is allowed in a pattern: 'a*c*e'\" >expect.six &&
	echo \"\" >>expect.six
	"

test_expect_success 'test disallow multiple asterisks in one word' '
	git config --add svn-remote.six.url "$svnrepo" &&
	git config --add svn-remote.six.fetch \
			 trunk:refs/remotes/six/trunk &&
	git config --add svn-remote.six.branches \
			 "branches/a*c*e:refs/remotes/six/branches/*" &&
	git config --add svn-remote.six.tags \
			 "tags/f*h*j:refs/remotes/six/tags/*" &&
	(
		cd tmp &&
		echo "try try" >>tags/fghij/src/b/readme &&
		poke tags/fghij/src/b/readme &&
		svn_cmd commit -m "try to try"
	) &&
	test_must_fail git svn fetch six 2>stderr.six &&
	test_cmp expect.six stderr.six
	'

test_done
