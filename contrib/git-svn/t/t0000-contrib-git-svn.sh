#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#


PATH=$PWD/../:$PATH
test_description='git-svn tests'
if test -d ../../../t
then
    cd ../../../t
else
    echo "Must be run in contrib/git-svn/t" >&2
    exit 1
fi

. ./test-lib.sh

GIT_DIR=$PWD/.git
GIT_SVN_DIR=$GIT_DIR/git-svn
SVN_TREE=$GIT_SVN_DIR/tree

svnadmin >/dev/null 2>&1
if test $? != 1
then
    test_expect_success 'skipping contrib/git-svn test' :
    test_done
    exit
fi

svn >/dev/null 2>&1
if test $? != 1
then
    test_expect_success 'skipping contrib/git-svn test' :
    test_done
    exit
fi

svnrepo=$PWD/svnrepo

set -e

svnadmin create $svnrepo
svnrepo="file://$svnrepo/test-git-svn"

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
svn import -m 'import for git-svn' . $svnrepo >/dev/null

cd ..

rm -rf import

test_expect_success \
    'initialize git-svn' \
    "git-svn init $svnrepo"

test_expect_success \
    'import an SVN revision into git' \
    'git-svn fetch'


name='try a deep --rmdir with a commit'
git checkout -b mybranch git-svn-HEAD
mv dir/a/b/c/d/e/file dir/file
cp dir/file file
git update-index --add --remove dir/a/b/c/d/e/file dir/file file
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch &&
     test -d $SVN_TREE/dir && test ! -d $SVN_TREE/dir/a"


name='detect node change from file to directory #1'
mkdir dir/new_file
mv dir/file dir/new_file/file
mv dir/new_file dir/file
git update-index --remove dir/file
git update-index --add dir/file/file
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch' \
    || true


name='detect node change from directory to file #1'
rm -rf dir $GIT_DIR/index
git checkout -b mybranch2 git-svn-HEAD
mv bar/zzz zzz
rm -rf bar
mv zzz bar
git update-index --remove -- bar/zzz
git update-index --add -- bar
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch2' \
    || true


name='detect node change from file to directory #2'
rm -f $GIT_DIR/index
git checkout -b mybranch3 git-svn-HEAD
rm bar/zzz
git-update-index --remove bar/zzz
mkdir bar/zzz
echo yyy > bar/zzz/yyy
git-update-index --add bar/zzz/yyy
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch3' \
    || true


name='detect node change from directory to file #2'
rm -f $GIT_DIR/index
git checkout -b mybranch4 git-svn-HEAD
rm -rf dir
git update-index --remove -- dir/file
touch dir
echo asdf > dir
git update-index --add -- dir
git commit -m "$name"

test_expect_code 1 "$name" \
    'git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch4' \
    || true


name='remove executable bit from a file'
rm -f $GIT_DIR/index
git checkout -b mybranch5 git-svn-HEAD
chmod -x exec.sh
git update-index exec.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch5 &&
     test ! -x $SVN_TREE/exec.sh"


name='add executable bit back file'
chmod +x exec.sh
git update-index exec.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch5 &&
     test -x $SVN_TREE/exec.sh"



name='executable file becomes a symlink to bar/zzz (file)'
rm exec.sh
ln -s bar/zzz exec.sh
git update-index exec.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch5 &&
     test -L $SVN_TREE/exec.sh"



name='new symlink is added to a file that was also just made executable'
chmod +x bar/zzz
ln -s bar/zzz exec-2.sh
git update-index --add bar/zzz exec-2.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch5 &&
     test -x $SVN_TREE/bar/zzz &&
     test -L $SVN_TREE/exec-2.sh"



name='modify a symlink to become a file'
git help > help || true
rm exec-2.sh
cp help exec-2.sh
git update-index exec-2.sh
git commit -m "$name"

test_expect_success "$name" \
    "git-svn commit --find-copies-harder --rmdir git-svn-HEAD..mybranch5 &&
     test -f $SVN_TREE/exec-2.sh &&
     test ! -L $SVN_TREE/exec-2.sh &&
     diff -u help $SVN_TREE/exec-2.sh"



name='test fetch functionality (svn => git) with alternate GIT_SVN_ID'
GIT_SVN_ID=alt
export GIT_SVN_ID
test_expect_success "$name" \
    "git-svn init $svnrepo && git-svn fetch -v &&
     git-rev-list --pretty=raw git-svn-HEAD | grep ^tree | uniq > a &&
     git-rev-list --pretty=raw alt-HEAD | grep ^tree | uniq > b &&
     diff -u a b"

test_done

