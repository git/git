#!/bin/sh

test_description='tests remote-svn'

. ./test-lib.sh

MARKSPATH=.git/info/fast-import/remote-svn

if ! test_have_prereq PYTHON
then
	skip_all='skipping remote-svn tests, python not available'
	test_done
fi

if test_have_prereq MINGW
then
	skip_all='skipping remote-svn tests for lack of POSIX'
	test_done
fi

# Override svnrdump with our simulator
PATH="$HOME:$PATH"
export PATH PYTHON_PATH GIT_BUILD_DIR

write_script "$HOME/svnrdump" <<\EOF
exec "$PYTHON_PATH" "$GIT_BUILD_DIR/contrib/svn-fe/svnrdump_sim.py" "$@"
EOF

init_git () {
	rm -fr .git &&
	git init &&
	#git remote add svnsim testsvn::sim:///$TEST_DIRECTORY/t9020/example.svnrdump
	# let's reuse an existing dump file!?
	git remote add svnsim "testsvn::sim://$TEST_DIRECTORY/t9154/svn.dump"
	git remote add svnfile "testsvn::file://$TEST_DIRECTORY/t9154/svn.dump"
}

if test -e "$GIT_BUILD_DIR/git-remote-testsvn"
then
	test_set_prereq REMOTE_SVN
fi

test_debug '
	git --version
	type git
	type svnrdump
'

test_expect_success REMOTE_SVN 'simple fetch' '
	init_git &&
	git fetch svnsim &&
	test_cmp .git/refs/svn/svnsim/master .git/refs/remotes/svnsim/master  &&
	cp .git/refs/remotes/svnsim/master master.good
'

test_debug '
	git show-ref -s refs/svn/svnsim/master
	git show-ref -s refs/remotes/svnsim/master
'

test_expect_success REMOTE_SVN 'repeated fetch, nothing shall change' '
	git fetch svnsim &&
	test_cmp master.good .git/refs/remotes/svnsim/master
'

test_expect_success REMOTE_SVN 'fetch from a file:// url gives the same result' '
	git fetch svnfile
'

test_expect_failure REMOTE_SVN 'the sha1 differ because the git-svn-id line in the commit msg contains the url' '
	test_cmp .git/refs/remotes/svnfile/master .git/refs/remotes/svnsim/master
'

test_expect_success REMOTE_SVN 'mark-file regeneration' '
	# filter out any other marks, that can not be regenerated. Only up to 3 digit revisions are allowed here
	grep ":[0-9]\{1,3\} " $MARKSPATH/svnsim.marks > $MARKSPATH/svnsim.marks.old &&
	rm $MARKSPATH/svnsim.marks &&
	git fetch svnsim &&
	test_cmp $MARKSPATH/svnsim.marks.old $MARKSPATH/svnsim.marks
'

test_expect_success REMOTE_SVN 'incremental imports must lead to the same head' '
	SVNRMAX=3 &&
	export SVNRMAX &&
	init_git &&
	git fetch svnsim &&
	test_cmp .git/refs/svn/svnsim/master .git/refs/remotes/svnsim/master  &&
	unset SVNRMAX &&
	git fetch svnsim &&
	test_cmp master.good .git/refs/remotes/svnsim/master
'

test_expect_success REMOTE_SVN 'respects configured default initial branch' '
	git -c init.defaultBranch=trunk remote add -f trunk \
		"testsvn::file://$TEST_DIRECTORY/t9154/svn.dump" &&
	git rev-parse --verify refs/remotes/trunk/trunk
'

test_debug 'git branch -a'

test_done
