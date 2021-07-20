#!/bin/sh
#
# Copyright (c) 2020 Google LLC
#

test_description='reftable/httpd interaction'

. ./test-lib.sh

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

REPO="$HTTPD_DOCUMENT_ROOT_PATH/repo"

test_expect_success 'serving ls-remote' '
	GIT_TEST_REFTABLE=1 git init -b main "$REPO" &&
	cd "$REPO" &&
	test_commit m1 &&
	>.git/git-daemon-export-ok &&
	git ls-remote "http://127.0.0.1:$LIB_HTTPD_PORT/smart/repo" > ls-remote.output &&
	cut -f 2-2 -d "	" <ls-remote.output >actual &&
	cat << EOF >expect &&
HEAD
refs/heads/main
refs/tags/m1
EOF
	test_cmp actual expect
'

test_done
