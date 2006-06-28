PATH=$PWD/../:$PATH
if test -d ../../../t
then
    cd ../../../t
else
    echo "Must be run in contrib/git-svn/t" >&2
    exit 1
fi

. ./test-lib.sh

GIT_DIR=$PWD/.git
GIT_SVN_DIR=$GIT_DIR/svn/git-svn
SVN_TREE=$GIT_SVN_DIR/svn-tree

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

if svnadmin create --help | grep fs-type >/dev/null
then
	svnadmin create --fs-type fsfs "$svnrepo"
else
	svnadmin create "$svnrepo"
fi

svnrepo="file://$svnrepo/test-git-svn"


