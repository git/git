#!/bin/sh

test_description='reftable HTTPD tests'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd

REPO="$HTTPD_DOCUMENT_ROOT_PATH/repo"

test_expect_success 'serving ls-remote' '
	git init --ref-format=reftable -b main "$REPO" &&
	cd "$REPO" &&
	test_commit m1 &&
	>.git/git-daemon-export-ok &&
	git ls-remote "http://127.0.0.1:$LIB_HTTPD_PORT/smart/repo" | cut -f 2-2 -d "	" >actual &&
	cat >expect <<-EOF &&
	HEAD
	refs/heads/main
	refs/tags/m1
	EOF
	test_cmp actual expect
'

test_done
