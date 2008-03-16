#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test http-push

This test runs various sanity checks on http-push.'

. ./test-lib.sh

ROOT_PATH="$PWD"
LIB_HTTPD_DAV=t

. ../lib-httpd.sh

if ! start_httpd >&3 2>&4
then
	say "skipping test, web server setup failed"
	test_done
	exit
fi

test_expect_success 'setup remote repository' '
	cd "$ROOT_PATH" &&
	mkdir test_repo &&
	cd test_repo &&
	git init &&
	: >path1 &&
	git add path1 &&
	test_tick &&
	git commit -m initial &&
	cd - &&
	git clone --bare test_repo test_repo.git &&
	cd test_repo.git &&
	git --bare update-server-info &&
	chmod +x hooks/post-update &&
	cd - &&
	mv test_repo.git $HTTPD_DOCUMENT_ROOT_PATH
'
	
test_expect_success 'clone remote repository' '
	cd "$ROOT_PATH" &&
	git clone $HTTPD_URL/test_repo.git test_repo_clone
'

test_expect_success 'push to remote repository' '
	cd "$ROOT_PATH"/test_repo_clone &&
	: >path2 &&
	git add path2 &&
	test_tick &&
	git commit -m path2 &&
	git push
'

test_expect_success 'create and delete remote branch' '
	cd "$ROOT_PATH"/test_repo_clone &&
	git checkout -b dev &&
	: >path3 &&
	git add path3 &&
	test_tick &&
	git commit -m dev &&
	git push origin dev &&
	git fetch &&
	git push origin :dev &&
	git branch -d -r origin/dev &&
	git fetch &&
	! git show-ref --verify refs/remotes/origin/dev
'

stop_httpd

test_done
