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

svn >/dev/null 2>&1
if test $? -ne 1
then
    test_expect_success 'skipping git-svn tests, svn not found' :
    test_done
    exit
fi

svnrepo=$PWD/svnrepo

perl -w -e "
use SVN::Core;
use SVN::Repos;
\$SVN::Core::VERSION gt '1.1.0' or exit(42);
system(qw/svnadmin create --fs-type fsfs/, '$svnrepo') == 0 or exit(41);
" >&3 2>&4
x=$?
if test $x -ne 0
then
	if test $x -eq 42; then
		err='Perl SVN libraries must be >= 1.1.0'
	elif test $x -eq 41; then
		err='svnadmin failed to create fsfs repository'
	else
		err='Perl SVN libraries not found or unusable, skipping test'
	fi
	test_expect_success "$err" :
	test_done
	exit
fi

svnrepo="file://$svnrepo"


