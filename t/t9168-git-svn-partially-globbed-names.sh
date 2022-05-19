#!/bin/sh
test_description='but svn globbing refspecs with prefixed globs'
. ./lib-but-svn.sh

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
		svn_cmd cummit -m "start a new branch" &&
		svn_cmd up &&
		echo "hi" >>branches/b_start/src/b/readme &&
		poke branches/b_start/src/b/readme &&
		echo "hey" >>branches/b_start/src/a/readme &&
		poke branches/b_start/src/a/readme &&
		svn_cmd cummit -m "hi" &&
		svn_cmd up &&
		svn_cmd cp branches/b_start tags/t_end &&
		echo "bye" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		echo "aye" >>tags/t_end/src/a/readme &&
		poke tags/t_end/src/a/readme &&
		svn_cmd cummit -m "the end" &&
		echo "byebye" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		svn_cmd cummit -m "nothing to see here"
	) &&
	but config --add svn-remote.svn.url "$svnrepo" &&
	but config --add svn-remote.svn.fetch \
			 "trunk/src/a:refs/remotes/trunk" &&
	but config --add svn-remote.svn.branches \
			 "branches/b_*/src/a:refs/remotes/branches/b_*" &&
	but config --add svn-remote.svn.tags\
			 "tags/t_*/src/a:refs/remotes/tags/t_*" &&
	but svn multi-fetch &&
	but log --pretty=oneline refs/remotes/tags/t_end >actual &&
	cut -d" " -f2- actual >output.end &&
	test_cmp expect.end output.end &&
	test "$(but rev-parse refs/remotes/tags/t_end~1)" = \
		"$(but rev-parse refs/remotes/branches/b_start)" &&
	test "$(but rev-parse refs/remotes/branches/b_start~2)" = \
		"$(but rev-parse refs/remotes/trunk)" &&
	test_must_fail but rev-parse refs/remotes/tags/t_end@3
	'

test_expect_success 'prepare test left-hand-side only prefixed globbing' '
	echo try to try >expect.two &&
	echo nothing to see here >>expect.two &&
	cat expect.end >>expect.two
	'

test_expect_success 'test left-hand-side only prefixed globbing' '
	but config --add svn-remote.two.url "$svnrepo" &&
	but config --add svn-remote.two.fetch trunk:refs/remotes/two/trunk &&
	but config --add svn-remote.two.branches \
			 "branches/b_*:refs/remotes/two/branches/*" &&
	but config --add svn-remote.two.tags \
			 "tags/t_*:refs/remotes/two/tags/*" &&
	(
		cd tmp &&
		echo "try try" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		svn_cmd cummit -m "try to try"
	) &&
	but svn fetch two &&
	but rev-list refs/remotes/two/tags/t_end >actual &&
	test_line_count = 6 actual &&
	but rev-list refs/remotes/two/branches/b_start >actual &&
	test_line_count = 3 actual &&
	test $(but rev-parse refs/remotes/two/branches/b_start~2) = \
	     $(but rev-parse refs/remotes/two/trunk) &&
	test $(but rev-parse refs/remotes/two/tags/t_end~3) = \
	     $(but rev-parse refs/remotes/two/branches/b_start) &&
	but log --pretty=oneline refs/remotes/two/tags/t_end >actual &&
	cut -d" " -f2- actual >output.two &&
	test_cmp expect.two output.two
	'

test_expect_success 'prepare test prefixed globs match just prefix' '
	cat >expect.three <<EOF
Tag cummit to t_
Branch cummit to b_
initial
EOF
	'

test_expect_success 'test prefixed globs match just prefix' '
	but config --add svn-remote.three.url "$svnrepo" &&
	but config --add svn-remote.three.fetch \
			 trunk:refs/remotes/three/trunk &&
	but config --add svn-remote.three.branches \
			 "branches/b_*:refs/remotes/three/branches/*" &&
	but config --add svn-remote.three.tags \
			 "tags/t_*:refs/remotes/three/tags/*" &&
	(
		cd tmp &&
		svn_cmd cp trunk branches/b_ &&
		echo "Branch cummit to b_" >>branches/b_/src/a/readme &&
		poke branches/b_/src/a/readme &&
		svn_cmd cummit -m "Branch cummit to b_" &&
		svn_cmd up && svn_cmd cp branches/b_ tags/t_ &&
		echo "Tag cummit to t_" >>tags/t_/src/a/readme &&
		poke tags/t_/src/a/readme &&
		svn_cmd cummit -m "Tag cummit to t_" &&
		svn_cmd up
	) &&
	but svn fetch three &&
	but rev-list refs/remotes/three/branches/b_ >actual &&
	test_line_count = 2 actual &&
	but rev-list refs/remotes/three/tags/t_ >actual &&
	test_line_count = 3 actual &&
	test $(but rev-parse refs/remotes/three/branches/b_~1) = \
	     $(but rev-parse refs/remotes/three/trunk) &&
	test $(but rev-parse refs/remotes/three/tags/t_~1) = \
	     $(but rev-parse refs/remotes/three/branches/b_) &&
	but log --pretty=oneline refs/remotes/three/tags/t_ >actual &&
	cut -d" " -f2- actual >output.three &&
	test_cmp expect.three output.three
	'

test_expect_success 'prepare test disallow prefixed multi-globs' "
cat >expect.four <<EOF
Only one set of wildcards (e.g. '*' or '*/*/*') is supported: branches/b_*/t/*

EOF
	"

test_expect_success 'test disallow prefixed multi-globs' '
	but config --add svn-remote.four.url "$svnrepo" &&
	but config --add svn-remote.four.fetch \
			 trunk:refs/remotes/four/trunk &&
	but config --add svn-remote.four.branches \
			 "branches/b_*/t/*:refs/remotes/four/branches/*" &&
	but config --add svn-remote.four.tags \
			 "tags/t_*/*:refs/remotes/four/tags/*" &&
	(
		cd tmp &&
		echo "try try" >>tags/t_end/src/b/readme &&
		poke tags/t_end/src/b/readme &&
		svn_cmd cummit -m "try to try"
	) &&
	test_must_fail but svn fetch four 2>stderr.four &&
	test_cmp expect.four stderr.four &&
	but config --unset svn-remote.four.branches &&
	but config --unset svn-remote.four.tags
	'

test_expect_success 'prepare test globbing in the middle of the word' '
	cat >expect.five <<EOF
Tag cummit to fghij
Branch cummit to abcde
initial
EOF
	'

test_expect_success 'test globbing in the middle of the word' '
	but config --add svn-remote.five.url "$svnrepo" &&
	but config --add svn-remote.five.fetch \
			 trunk:refs/remotes/five/trunk &&
	but config --add svn-remote.five.branches \
			 "branches/a*e:refs/remotes/five/branches/*" &&
	but config --add svn-remote.five.tags \
			 "tags/f*j:refs/remotes/five/tags/*" &&
	(
		cd tmp &&
		svn_cmd cp trunk branches/abcde &&
		echo "Branch cummit to abcde" >>branches/abcde/src/a/readme &&
		poke branches/b_/src/a/readme &&
		svn_cmd cummit -m "Branch cummit to abcde" &&
		svn_cmd up &&
		svn_cmd cp branches/abcde tags/fghij &&
		echo "Tag cummit to fghij" >>tags/fghij/src/a/readme &&
		poke tags/fghij/src/a/readme &&
		svn_cmd cummit -m "Tag cummit to fghij" &&
		svn_cmd up
	) &&
	but svn fetch five &&
	but rev-list refs/remotes/five/branches/abcde >actual &&
	test_line_count = 2 actual &&
	but rev-list refs/remotes/five/tags/fghij >actual &&
	test_line_count = 3 actual &&
	test $(but rev-parse refs/remotes/five/branches/abcde~1) = \
	     $(but rev-parse refs/remotes/five/trunk) &&
	test $(but rev-parse refs/remotes/five/tags/fghij~1) = \
	     $(but rev-parse refs/remotes/five/branches/abcde) &&
	but log --pretty=oneline refs/remotes/five/tags/fghij >actual &&
	cut -d" " -f2- actual >output.five &&
	test_cmp expect.five output.five
	'

test_expect_success 'prepare test disallow multiple asterisks in one word' "
	echo \"Only one '*' is allowed in a pattern: 'a*c*e'\" >expect.six &&
	echo \"\" >>expect.six
	"

test_expect_success 'test disallow multiple asterisks in one word' '
	but config --add svn-remote.six.url "$svnrepo" &&
	but config --add svn-remote.six.fetch \
			 trunk:refs/remotes/six/trunk &&
	but config --add svn-remote.six.branches \
			 "branches/a*c*e:refs/remotes/six/branches/*" &&
	but config --add svn-remote.six.tags \
			 "tags/f*h*j:refs/remotes/six/tags/*" &&
	(
		cd tmp &&
		echo "try try" >>tags/fghij/src/b/readme &&
		poke tags/fghij/src/b/readme &&
		svn_cmd cummit -m "try to try"
	) &&
	test_must_fail but svn fetch six 2>stderr.six &&
	test_cmp expect.six stderr.six
	'

test_done
