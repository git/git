test_description='git-svn rmdir'
. ./lib-git-svn.sh

test_expect_success 'initialize repo' "
	mkdir import &&
	cd import &&
	mkdir -p deeply/nested/directory/number/1 &&
	mkdir -p deeply/nested/directory/number/2 &&
	echo foo > deeply/nested/directory/number/1/file &&
	echo foo > deeply/nested/directory/number/2/another &&
	svn import -m 'import for git-svn' . $svnrepo &&
	cd ..
	"

test_expect_success 'mirror via git-svn' "
	git-svn init $svnrepo &&
	git-svn fetch &&
	git checkout -f -b test-rmdir remotes/git-svn
	"

test_expect_success 'Try a commit on rmdir' "
	git rm -f deeply/nested/directory/number/2/another &&
	git commit -a -m 'remove another' &&
	git-svn set-tree --rmdir HEAD &&
	svn ls -R $svnrepo | grep ^deeply/nested/directory/number/1
	"


test_done
