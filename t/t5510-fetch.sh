#!/bin/sh
# Copyright (c) 2006, Junio C Hamano.

test_description='Per branch config variables affects "git fetch".

'

. ./test-lib.sh

D=`pwd`

test_expect_success setup '
	echo >file original &&
	git add file &&
	git commit -a -m original'

test_expect_success "clone and setup child repos" '
	git clone . one &&
	cd one &&
	echo >file updated by one &&
	git commit -a -m "updated by one" &&
	cd .. &&
	git clone . two &&
	cd two &&
	git repo-config branch.master.remote one &&
	{
		echo "URL: ../one/.git/"
		echo "Pull: refs/heads/master:refs/heads/one"
	} >.git/remotes/one
'

test_expect_success "fetch test" '
	cd "$D" &&
	echo >file updated by origin &&
	git commit -a -m "updated by origin" &&
	cd two &&
	git fetch &&
	test -f .git/refs/heads/one &&
	mine=`git rev-parse refs/heads/one` &&
	his=`cd ../one && git rev-parse refs/heads/master` &&
	test "z$mine" = "z$his"
'

test_done
