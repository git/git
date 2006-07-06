. ./test-lib.sh

if test -n "$NO_SVN_TESTS"
then
	test_expect_success 'skipping git-svn tests, NO_SVN_TESTS defined' :
	test_done
	exit
fi

GIT_DIR=$PWD/.git
GIT_SVN_DIR=$GIT_DIR/svn/git-svn
SVN_TREE=$GIT_SVN_DIR/svn-tree

svnadmin >/dev/null 2>&1
if test $? != 1
then
    test_expect_success 'skipping git-svn tests, svnadmin not found' :
    test_done
    exit
fi

svn >/dev/null 2>&1
if test $? != 1
then
    test_expect_success 'skipping git-svn tests, svn not found' :
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


