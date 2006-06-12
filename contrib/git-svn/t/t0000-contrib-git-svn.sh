#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='git-svn tests'
GIT_SVN_LC_ALL=$LC_ALL
. ./lib-git-svn.sh

mkdir import
cd import

echo foo > foo
ln -s foo foo.link
mkdir -p dir/a/b/c/d/e
echo 'deep dir' > dir/a/b/c/d/e/file
mkdir -p bar
echo 'zzz' > bar/zzz
echo '#!/bin/sh' > exec.sh
chmod +x exec.sh
svn import -m 'import for git-svn' . "$svnrepo" >/dev/null

cd ..
rm -rf import

test_expect_success \
    'initialize git-svn' \
    "git-svn init $svnrepo"

test_expect_success \
    'import an SVN revision into git' \
    'git-svn fetch'


name='try a deep --rmdir with a commit'
git checkout -f -b mybranch remotes/git-svn
mv dir/a/b/c/d/e/file dir/file
cp dir/file file
git update-index --add --remove dir/a/b/c/d/e/file dir/file file
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch &&
     test -d $SVN_TREE/dir && test ! -d $SVN_TREE/dir/a"


name='detect node change from file to directory #1'
mkdir dir/new_file
mv dir/file dir/new_file/file
mv dir/new_file dir/file
git update-index --remove dir/file
git update-index --add dir/file/file
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch' \
    || true


name='detect node change from directory to file #1'
rm -rf dir $GIT_DIR/index
git checkout -f -b mybranch2 remotes/git-svn
mv bar/zzz zzz
rm -rf bar
mv zzz bar
git update-index --remove -- bar/zzz
git update-index --add -- bar
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch2' \
    || true


name='detect node change from file to directory #2'
rm -f $GIT_DIR/index
git checkout -f -b mybranch3 remotes/git-svn
rm bar/zzz
git-update-index --remove bar/zzz
mkdir bar/zzz
echo yyy > bar/zzz/yyy
git-update-index --add bar/zzz/yyy
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch3' \
    || true


name='detect node change from directory to file #2'
rm -f $GIT_DIR/index
git checkout -f -b mybranch4 remotes/git-svn
rm -rf dir
git update-index --remove -- dir/file
touch dir
echo asdf > dir
git update-index --add -- dir
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch4' \
    || true


name='remove executable bit from a file'
rm -f $GIT_DIR/index
git checkout -f -b mybranch5 remotes/git-svn
chmod -x exec.sh
git update-index exec.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch5 &&
     test ! -x $SVN_TREE/exec.sh"


name='add executable bit back file'
chmod +x exec.sh
git update-index exec.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch5 &&
     test -x $SVN_TREE/exec.sh"



name='executable file becomes a symlink to bar/zzz (file)'
rm exec.sh
ln -s bar/zzz exec.sh
git update-index exec.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch5 &&
     test -L $SVN_TREE/exec.sh"



name='new symlink is added to a file that was also just made executable'
chmod +x bar/zzz
ln -s bar/zzz exec-2.sh
git update-index --add bar/zzz exec-2.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch5 &&
     test -x $SVN_TREE/bar/zzz &&
     test -L $SVN_TREE/exec-2.sh"



name='modify a symlink to become a file'
git help > help || true
rm exec-2.sh
cp help exec-2.sh
git update-index exec-2.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir remotes/git-svn..mybranch5 &&
     test -f $SVN_TREE/exec-2.sh &&
     test ! -L $SVN_TREE/exec-2.sh &&
     diff -u help $SVN_TREE/exec-2.sh"


if test -n "$GIT_SVN_LC_ALL" && echo $GIT_SVN_LC_ALL | grep -q '\.UTF-8$'
then
	name="commit with UTF-8 message: locale: $GIT_SVN_LC_ALL"
	echo '# hello' >> exec-2.sh
	git update-index exec-2.sh
	git commit -m 'éï∏'
	export LC_ALL="$GIT_SVN_LC_ALL"
	test_expect_success "$name" "git-svn commit HEAD"
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

test_done

