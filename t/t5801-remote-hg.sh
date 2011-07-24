#!/bin/sh
#
# Copyright (c) 2010 Sverre Rabbelier
#

test_description='Test remote-helper import and export commands'

. ./test-lib.sh

if test_have_prereq PYTHON && "$PYTHON_PATH" -c '
import sys
if sys.hexversion < 0x02040000:
    sys.exit(1)
'
then
	:
else
	skip_all='skipping git remote-hg tests: requires Python 2.4 or newer'
	test_done
fi

# Call cmp with the arguments -x ".hg" -x ".git" <left> <right>

vcs_cmp () {
	$DIFF -u -x ".hg" -x ".git" $1 $2
}

ROOT=$PWD

test_expect_success 'setup repository' '
	printf "[ui]\nusername = A U Thor <author@example.com>" > \
		${HOME}/.hgrc &&
	mkdir server &&
	hg init server/.hg &&
	hg clone "$ROOT/server" public &&
	(cd public &&
	 echo content >file &&
	 hg add file &&
	 hg commit -m one &&
	 hg push)
'

test_expect_success 'cloning from local repo' '
	git clone "hg::file://${ROOT}/server" localclone &&
	vcs_cmp public localclone
'

test_expect_success 'cloning from remote repo' '
	git clone "hg::remote://${ROOT}/server" clone &&
	vcs_cmp public clone
'

test_expect_success 'create new commit on remote' '
	(cd public &&
	 echo content >>file &&
	 hg commit -A -m two &&
	 hg push)
'

test_expect_success 'pulling from local repo' '
	(cd localclone && git pull) &&
	vcs_cmp public localclone
'

test_expect_success 'pulling from remote remote' '
	(cd clone && git pull) &&
	vcs_cmp public clone
'

test_expect_success 'pushing to local empty repo' '
	hg init localempty &&
	(cd localclone &&
	git push --all "hg::file://${ROOT}/localempty") &&
	(cd localempty &&
	hg up tip) &&
	vcs_cmp localclone localempty
'

test_expect_success 'pushing to remote empty repo' '
	hg init empty &&
	(cd localclone &&
	git push --all "hg::remote://${ROOT}/empty") &&
	(cd empty &&
	hg up tip) &&
	vcs_cmp localclone empty
'

test_expect_success 'pushing to local repo' '
	(cd localclone &&
	echo content >>file &&
	git commit -a -m three &&
	git push) &&
	(cd server &&
	hg up tip) &&
	vcs_cmp localclone server
'

test_expect_success 'synch with changes from localclone' '
	(cd clone &&
	 git pull)
'

test_expect_success 'pushing remote local repo' '
	(cd clone &&
	echo content >>file &&
	git commit -a -m four &&
	git push) &&
	(cd server &&
	hg up tip) &&
	vcs_cmp clone server
'

test_expect_success 'creating new branch' '
	(cd public &&
	hg branch different-branch &&
	echo different >> file &&
	hg commit -m five &&
	hg push -f)
'

test_expect_success 'pull in new branch to local repository' '
	(cd localclone &&
	git fetch origin default &&
	test_must_fail git rev-parse -q --verify refs/remotes/origin/different-branch &&
	git fetch &&
	git rev-parse --no-revs --verify refs/remotes/origin/different-branch)
'

test_expect_success 'pull in new branch to remote repository' '
	(cd clone &&
	git fetch origin default &&
	test_must_fail git rev-parse -q --verify refs/remotes/origin/different-branch &&
	git fetch &&
	git rev-parse --no-revs --verify refs/remotes/origin/different-branch)
'

test_done
