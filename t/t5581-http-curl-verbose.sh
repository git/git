#!/bin/sh

test_description='test BUT_CURL_VERBOSE'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	mkdir "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" --bare init &&
	but config push.default matching &&
	echo content >file &&
	but add file &&
	but cummit -m one &&
	but remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but push public main:main
'

test_expect_success 'failure in but-upload-pack is shown' '
	test_might_fail env BUT_CURL_VERBOSE=1 \
		but clone "$HTTPD_URL/error_but_upload_pack/smart/repo.but" \
		2>curl_log &&
	grep "<= Recv header: HTTP/1.1 500 Intentional Breakage" curl_log
'

test_done
