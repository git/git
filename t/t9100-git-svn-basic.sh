#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git-svn basic tests'
GIT_SVN_LC_ALL=$LC_ALL

case "$LC_ALL" in
*.UTF-8)
	have_utf8=t
	;;
*)
	have_utf8=
	;;
esac

. ./lib-git-svn.sh

echo 'define NO_SVN_TESTS to skip git-svn tests'

test_expect_success \
    'initialize git-svn' "
	mkdir import &&
	cd import &&
	echo foo > foo &&
	ln -s foo foo.link
	mkdir -p dir/a/b/c/d/e &&
	echo 'deep dir' > dir/a/b/c/d/e/file &&
	mkdir bar &&
	echo 'zzz' > bar/zzz &&
	echo '#!/bin/sh' > exec.sh &&
	chmod +x exec.sh &&
	svn import -m 'import for git-svn' . $svnrepo >/dev/null &&
	cd .. &&
	rm -rf import &&
	git-svn init $svnrepo"

test_expect_success \
    'import an SVN revision into git' \
    'git-svn fetch'

test_expect_success "checkout from svn" "svn co $svnrepo '$SVN_TREE'"

name='try a deep --rmdir with a commit'
test_expect_success "$name" "
	git checkout -f -b mybranch remotes/git-svn &&
	mv dir/a/b/c/d/e/file dir/file &&
	cp dir/file file &&
	git update-index --add --remove dir/a/b/c/d/e/file dir/file file &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch &&
	svn up '$SVN_TREE' &&
	test -d '$SVN_TREE'/dir && test ! -d '$SVN_TREE'/dir/a"


name='detect node change from file to directory #1'
test_expect_failure "$name" "
	mkdir dir/new_file &&
	mv dir/file dir/new_file/file &&
	mv dir/new_file dir/file &&
	git update-index --remove dir/file &&
	git update-index --add dir/file/file &&
	git commit -m '$name'  &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch" || true


name='detect node change from directory to file #1'
test_expect_failure "$name" "
	rm -rf dir '$GIT_DIR'/index &&
	git checkout -f -b mybranch2 remotes/git-svn &&
	mv bar/zzz zzz &&
	rm -rf bar &&
	mv zzz bar &&
	git update-index --remove -- bar/zzz &&
	git update-index --add -- bar &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch2" || true


name='detect node change from file to directory #2'
test_expect_failure "$name" "
	rm -f '$GIT_DIR'/index &&
	git checkout -f -b mybranch3 remotes/git-svn &&
	rm bar/zzz &&
	git-update-index --remove bar/zzz &&
	mkdir bar/zzz &&
	echo yyy > bar/zzz/yyy &&
	git-update-index --add bar/zzz/yyy &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch3" || true


name='detect node change from directory to file #2'
test_expect_failure "$name" "
	rm -f '$GIT_DIR'/index &&
	git checkout -f -b mybranch4 remotes/git-svn &&
	rm -rf dir &&
	git update-index --remove -- dir/file &&
	touch dir &&
	echo asdf > dir &&
	git update-index --add -- dir &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch4" || true


name='remove executable bit from a file'
test_expect_success "$name" "
	rm -f '$GIT_DIR'/index &&
	git checkout -f -b mybranch5 remotes/git-svn &&
	chmod -x exec.sh &&
	git update-index exec.sh &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch5 &&
	svn up '$SVN_TREE' &&
	test ! -x '$SVN_TREE'/exec.sh"


name='add executable bit back file'
test_expect_success "$name" "
	chmod +x exec.sh &&
	git update-index exec.sh &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch5 &&
	svn up '$SVN_TREE' &&
	test -x '$SVN_TREE'/exec.sh"


name='executable file becomes a symlink to bar/zzz (file)'
test_expect_success "$name" "
	rm exec.sh &&
	ln -s bar/zzz exec.sh &&
	git update-index exec.sh &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch5 &&
	svn up '$SVN_TREE' &&
	test -L '$SVN_TREE'/exec.sh"

name='new symlink is added to a file that was also just made executable'

test_expect_success "$name" "
	chmod +x bar/zzz &&
	ln -s bar/zzz exec-2.sh &&
	git update-index --add bar/zzz exec-2.sh &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch5 &&
	svn up '$SVN_TREE' &&
	test -x '$SVN_TREE'/bar/zzz &&
	test -L '$SVN_TREE'/exec-2.sh"

name='modify a symlink to become a file'
test_expect_success "$name" "
	echo git help > help || true &&
	rm exec-2.sh &&
	cp help exec-2.sh &&
	git update-index exec-2.sh &&
	git commit -m '$name' &&
	git-svn set-tree --find-copies-harder --rmdir \
		remotes/git-svn..mybranch5 &&
	svn up '$SVN_TREE' &&
	test -f '$SVN_TREE'/exec-2.sh &&
	test ! -L '$SVN_TREE'/exec-2.sh &&
	diff -u help $SVN_TREE/exec-2.sh"

if test "$have_utf8" = t
then
	name="commit with UTF-8 message: locale: $GIT_SVN_LC_ALL"
	LC_ALL="$GIT_SVN_LC_ALL"
	export LC_ALL
	test_expect_success "$name" "
		echo '# hello' >> exec-2.sh &&
		git update-index exec-2.sh &&
		git commit -m 'éï∏' &&
		git-svn set-tree HEAD"
	unset LC_ALL
else
	echo "UTF-8 locale not set, test skipped ($GIT_SVN_LC_ALL)"
fi

name='test fetch functionality (svn => git) with alternate GIT_SVN_ID'
GIT_SVN_ID=alt
export GIT_SVN_ID
test_expect_success "$name" \
    "git-svn init $svnrepo && git-svn fetch &&
     git-rev-list --pretty=raw remotes/git-svn | grep ^tree | uniq > a &&
     git-rev-list --pretty=raw remotes/alt | grep ^tree | uniq > b &&
     diff -u a b"

name='check imported tree checksums expected tree checksums'
rm -f expected
if test "$have_utf8" = t
then
	echo tree bf522353586b1b883488f2bc73dab0d9f774b9a9 > expected
fi
cat >> expected <<\EOF
tree 83654bb36f019ae4fe77a0171f81075972087624
tree 031b8d557afc6fea52894eaebb45bec52f1ba6d1
tree 0b094cbff17168f24c302e297f55bfac65eb8bd3
tree d667270a1f7b109f5eb3aaea21ede14b56bfdd6e
tree 56a30b966619b863674f5978696f4a3594f2fca9
tree d667270a1f7b109f5eb3aaea21ede14b56bfdd6e
tree 8f51f74cf0163afc9ad68a4b1537288c4558b5a4
EOF

echo tree 4b825dc642cb6eb9a060e54bf8d69288fbee4904 >> expected

test_expect_success "$name" "diff -u a expected"

test_done
