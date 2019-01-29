#!/bin/sh

test_description='test GIT_CURL_VERBOSE'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	mkdir "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" --bare init &&
	git config push.default matching &&
	echo content >file &&
	git add file &&
	git commit -m one &&
	git remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push public master:master
'

test_expect_success 'failure in git-upload-pack is shown' '
	test_might_fail env GIT_CURL_VERBOSE=1 \
		git clone "$HTTPD_URL/error_git_upload_pack/smart/repo.git" \
		2>curl_log &&
	grep "< HTTP/1.1 500 Intentional Breakage" curl_log
'

stop_httpd

test_done
