#!/bin/sh
#
# Copyright (c) 2010 Sverre Rabbelier
#

test_description='Test remote-helper import and export commands'

. ./test-lib.sh

if ! test_have_prereq PYTHON ; then
	skip_all='skipping git-remote-hg tests, python not available'
	test_done
fi

"$PYTHON_PATH" -c '
import sys
if sys.hexversion < 0x02040000:
    sys.exit(1)
' || {
	skip_all='skipping git-remote-hg tests, python version < 2.4'
	test_done
}

compare_refs() {
	git --git-dir="$1/.git" rev-parse --verify $2 >expect &&
	git --git-dir="$3/.git" rev-parse --verify $4 >actual &&
	test_cmp expect actual
}

test_expect_success 'setup repository' '
	git init --bare server/.git &&
	git clone server public &&
	(cd public &&
	 echo content >file &&
	 git add file &&
	 git commit -m one &&
	 git push origin master)
'

test_expect_success 'cloning from local repo' '
	git clone "testgit::${PWD}/server" localclone &&
	test_cmp public/file localclone/file
'

test_expect_success 'cloning from remote repo' '
	git clone "testgit::file://${PWD}/server" clone &&
	test_cmp public/file clone/file
'

test_expect_success 'create new commit on remote' '
	(cd public &&
	 echo content >>file &&
	 git commit -a -m two &&
	 git push)
'

test_expect_success 'pulling from local repo' '
	(cd localclone && git pull) &&
	test_cmp public/file localclone/file
'

test_expect_success 'pulling from remote remote' '
	(cd clone && git pull) &&
	test_cmp public/file clone/file
'

test_expect_success 'pushing to local repo' '
	(cd localclone &&
	echo content >>file &&
	git commit -a -m three &&
	git push) &&
	compare_refs localclone HEAD server HEAD
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
	compare_refs clone HEAD server HEAD
'

test_done
