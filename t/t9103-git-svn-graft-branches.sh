test_description='git-svn graft-branches'
. ./lib-git-svn.sh

svnrepo="$svnrepo/test-git-svn"

test_expect_success 'initialize repo' "
	mkdir import &&
	cd import &&
	mkdir -p trunk branches tags &&
	echo hello > trunk/readme &&
	svn import -m 'import for git-svn' . $svnrepo &&
	cd .. &&
	svn cp -m 'tag a' $svnrepo/trunk $svnrepo/tags/a &&
	svn cp -m 'branch a' $svnrepo/trunk $svnrepo/branches/a &&
	svn co $svnrepo wc &&
	cd wc &&
	echo feedme >> branches/a/readme &&
	svn commit -m hungry &&
	cd trunk &&
	svn merge -r3:4 $svnrepo/branches/a &&
	svn commit -m 'merge with a' &&
	cd ../.. &&
	git-svn multi-init $svnrepo -T trunk -b branches -t tags &&
	git-svn multi-fetch
	"

r1=`git-rev-list remotes/trunk | tail -n1`
r2=`git-rev-list remotes/tags/a | tail -n1`
r3=`git-rev-list remotes/a | tail -n1`
r4=`git-rev-parse remotes/a`
r5=`git-rev-parse remotes/trunk`

test_expect_success 'test graft-branches regexes and copies' "
	test -n "$r1" &&
	test -n "$r2" &&
	test -n "$r3" &&
	test -n "$r4" &&
	test -n "$r5" &&
	git-svn graft-branches &&
	grep '^$r2 $r1' $GIT_DIR/info/grafts &&
	grep '^$r3 $r1' $GIT_DIR/info/grafts &&
	grep '^$r5 ' $GIT_DIR/info/grafts | grep '$r4' | grep '$r1'
	"

test_debug 'gitk --all & sleep 1'

test_expect_success 'test graft-branches with tree-joins' "
	rm $GIT_DIR/info/grafts &&
	git-svn graft-branches --no-default-regex --no-graft-copy -B &&
	grep '^$r3 ' $GIT_DIR/info/grafts | grep '$r1' | grep '$r2' &&
	grep '^$r2 $r1' $GIT_DIR/info/grafts &&
	grep '^$r5 ' $GIT_DIR/info/grafts | grep '$r1' | grep '$r4'
	"

# the result of this is kinda funky, we have a strange history and
# this is just a test :)
test_debug 'gitk --all &'

test_done
