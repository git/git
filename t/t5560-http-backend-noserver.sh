#!/bin/sh

test_description='test git-http-backend-noserver'
. ./test-lib.sh

HTTPD_DOCUMENT_ROOT_PATH="$TRASH_DIRECTORY"

run_backend() {
	echo "$2" |
	QUERY_STRING="${1#*\?}" \
	GIT_PROJECT_ROOT="$HTTPD_DOCUMENT_ROOT_PATH" \
	PATH_INFO="${1%%\?*}" \
	git http-backend >act.out 2>act.err
}

GET() {
	export REQUEST_METHOD="GET" &&
	run_backend "/repo.git/$1" &&
	unset REQUEST_METHOD &&
	if ! grep "Status" act.out >act
	then
		printf "Status: 200 OK\r\n" >act
	fi
	printf "Status: $2\r\n" >exp &&
	test_cmp exp act
}

POST() {
	export REQUEST_METHOD="POST" &&
	export CONTENT_TYPE="application/x-$1-request" &&
	run_backend "/repo.git/$1" "$2" &&
	unset REQUEST_METHOD &&
	unset CONTENT_TYPE &&
	if ! grep "Status" act.out >act
	then
		printf "Status: 200 OK\r\n" >act
	fi
	printf "Status: $3\r\n" >exp &&
	test_cmp exp act
}

log_div() {
	return 0
}

. "$TEST_DIRECTORY"/t556x_common

expect_aliased() {
	export REQUEST_METHOD="GET" &&
	if test $1 = 0; then
		run_backend "$2"
	else
		run_backend "$2" &&
		echo "fatal: '$2': aliased" >exp.err &&
		test_cmp exp.err act.err
	fi
	unset REQUEST_METHOD
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
