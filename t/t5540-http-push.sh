#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test http-push

This test runs various sanity checks on http-push.'

. ./test-lib.sh

ROOT_PATH="$PWD"
LIB_HTTPD_DAV=t
LIB_HTTPD_PORT=${LIB_HTTPD_PORT-'5540'}

if git http-push > /dev/null 2>&1 || [ $? -eq 128 ]
then
	say "skipping test, USE_CURL_MULTI is not defined"
	test_done
fi

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

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
	mv hooks/post-update.sample hooks/post-update &&
	cd - &&
	mv test_repo.git "$HTTPD_DOCUMENT_ROOT_PATH"
'

test_expect_success 'clone remote repository' '
	cd "$ROOT_PATH" &&
	git clone $HTTPD_URL/test_repo.git test_repo_clone
'

test_expect_failure 'push to remote repository with packed refs' '
	cd "$ROOT_PATH"/test_repo_clone &&
	: >path2 &&
	git add path2 &&
	test_tick &&
	git commit -m path2 &&
	HEAD=$(git rev-parse --verify HEAD) &&
	git push &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
'

test_expect_success ' push to remote repository with unpacked refs' '
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 rm packed-refs &&
	 git update-ref refs/heads/master \
		0c973ae9bd51902a28466f3850b543fa66a6aaf4) &&
	git push &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
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
	test_must_fail git show-ref --verify refs/remotes/origin/dev
'

test_expect_success 'MKCOL sends directory names with trailing slashes' '

	! grep "\"MKCOL.*[^/] HTTP/[^ ]*\"" < "$HTTPD_ROOT_PATH"/access.log

'

x1="[0-9a-f]"
x2="$x1$x1"
x5="$x1$x1$x1$x1$x1"
x38="$x5$x5$x5$x5$x5$x5$x5$x1$x1$x1"
x40="$x38$x2"

test_expect_success 'PUT and MOVE sends object to URLs with SHA-1 hash suffix' '
	sed -e "s/PUT /OP /" -e "s/MOVE /OP /" "$HTTPD_ROOT_PATH"/access.log |
	grep -e "\"OP .*/objects/$x2/${x38}_$x40 HTTP/[.0-9]*\" 20[0-9] "

'

stop_httpd

test_done
