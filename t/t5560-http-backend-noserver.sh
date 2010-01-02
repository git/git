#!/bin/sh

test_description='test git-http-backend-noserver'
. ./test-lib.sh

HTTPD_DOCUMENT_ROOT_PATH="$TRASH_DIRECTORY"

run_backend() {
	REQUEST_METHOD=GET \
	GIT_PROJECT_ROOT="$HTTPD_DOCUMENT_ROOT_PATH" \
	PATH_INFO="$1" \
	git http-backend >act.out 2>act.err
}

GET() {
	return 0
}

POST() {
	return 0
}

log_div() {
	return 0
}

. "$TEST_DIRECTORY"/t556x_common

expect_aliased() {
	if test $1 = 0; then
		run_backend "$2"
	else
		run_backend "$2" &&
		echo "fatal: '$2': aliased" >exp.err &&
		test_cmp exp.err act.err
	fi
}

test_expect_success 'http-backend blocks bad PATH_INFO' '
	config http.getanyfile true &&

	expect_aliased 0 /repo.git/HEAD &&

	expect_aliased 1 /repo.git/../HEAD &&
	expect_aliased 1 /../etc/passwd &&
	expect_aliased 1 ../etc/passwd &&
	expect_aliased 1 /etc//passwd &&
	expect_aliased 1 /etc/./passwd &&
	expect_aliased 1 //domain/data.txt
'

test_done
