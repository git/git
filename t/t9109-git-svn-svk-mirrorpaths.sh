#!/bin/sh
#
# Copyright (c) 2006 Sam Vilian
#

test_description='git-svn on SVK mirror paths'
. ./lib-git-svn.sh

# ok, people who don't have SVK installed probably don't care about
# this test.

# we set up the repository manually, because even if SVK is installed
# it is difficult to use it in a way that is idempotent.

# we are not yet testing merge tickets..

uuid=b00bface-b1ff-c0ff-f0ff-b0bafe775e1e
url=https://really.slow.server.com/foobar

test_expect_success 'initialize repo' "
	git config svn-remote.svn.useSvmProps true &&

	echo '#!/bin/sh' > $rawsvnrepo/hooks/pre-revprop-change &&
	echo 'exit 0' >> $rawsvnrepo/hooks/pre-revprop-change &&
	chmod +x $rawsvnrepo/hooks/pre-revprop-change &&

	mkdir import &&
	cd import &&
	mkdir local &&
	echo hello > local/readme &&
	svn import -m 'random local work' . $svnrepo &&
	cd .. &&

	svn co $svnrepo wc &&
	cd wc &&
	mkdir -p mirror/foobar &&
	svn add mirror &&
	svn ps svm:source $url mirror/foobar &&
	svn ps svm:uuid $uuid mirror/foobar &&
	svn ps svm:mirror / mirror/foobar &&
	svn commit -m 'setup mirror/foobar as mirror of upstream' &&
	svn ps -r 2 --revprop svm:headrev $uuid:0 $svnrepo &&

	mkdir mirror/foobar/trunk
	echo hello, world > mirror/foobar/trunk/readme &&
	svn add mirror/foobar/trunk &&
	svn commit -m 'first upstream revision' &&
	svn ps -r 3 --revprop svm:headrev $uuid:1 $svnrepo &&

	svn up &&
	svn mkdir mirror/foobar/branches &&
	svn cp mirror/foobar/trunk mirror/foobar/branches/silly &&
	svn commit -m 'make branch for silliness' &&
	svn ps -r 4 --revprop svm:headrev $uuid:2 $svnrepo &&

	svn up &&
	echo random untested feature >> mirror/foobar/trunk/readme &&
	poke mirror/foobar/trunk/readme &&
	svn commit -m 'add a c00l feature to trunk' &&
	svn ps -r 5 --revprop svm:headrev $uuid:3 $svnrepo &&

	svn up &&
	echo bug fix >> mirror/foobar/branches/silly/readme &&
	poke mirror/foobar/branches/silly/readme &&
	svn commit -m 'fix a bug' &&
	svn ps -r 6 --revprop svm:headrev $uuid:4 $svnrepo &&

	svn mkdir mirror/foobar/tags &&
	svn cp mirror/foobar/branches/silly mirror/foobar/tags/blah-1.0 &&
	svn commit -m 'make a release' &&
	svn ps -r 7 --revprop svm:headrev $uuid:5 $svnrepo &&

	cd ..
	"

test_expect_success 'init an SVK mirror path' "
	git-svn init -T trunk -t tags -b branches $svnrepo/mirror/foobar
	"

test_expect_success 'multi-fetch an SVK mirror path' "git-svn multi-fetch"

test_expect_success 'got tag history OK' "
	test \`git-log --pretty=oneline remotes/tags/blah-1.0 | wc -l\` -eq 3
	"

test_expect_success 're-wrote git-svn-id URL, revision and UUID' "
	git cat-file commit refs/remotes/trunk | \
	    fgrep 'git-svn-id: $url/mirror/foobar/trunk@3 $uuid' &&
	git cat-file commit refs/remotes/tags/blah-1.0 | \
	    fgrep 'git-svn-id: $url/mirror/foobar/tags/blah-1.0@5 $uuid'
	git cat-file commit refs/remotes/silly | \
	    fgrep 'git-svn-id: $url/mirror/foobar/branches/silly@4 $uuid'
	"

test_expect_success 're-wrote author e-mail domain UUID' "
	test \`git log --pretty=fuller trunk | \
	       grep '<.*@.*>' | fgrep '@$uuid>' | wc -l\` -eq 4 &&
	test \`git log --pretty=fuller remotes/silly | \
	       grep '<.*@.*>' | fgrep '@$uuid>' | wc -l\` -eq 6 &&
	test \`git log --pretty=fuller remotes/tags/blah-1.0 | \
	       grep '<.*@.*>' | fgrep '@$uuid>' | wc -l\` -eq 6
	"

test_debug 'gitk --all &'

test_done
