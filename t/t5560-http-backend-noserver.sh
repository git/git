#!/bin/sh

test_description='test git-http-backend-noserver'
. ./test-lib.sh

HTTPD_DOCUMENT_ROOT_PATH="$TRASH_DIRECTORY"

if test_have_prereq GREP_STRIPS_CR
then
	GREP_OPTIONS=-U
	export GREP_OPTIONS
fi

run_backend() {
	echo "$2" |
	QUERY_STRING="${1#*[?]}" \
	PATH_TRANSLATED="$HTTPD_DOCUMENT_ROOT_PATH/${1%%[?]*}" \
	git http-backend >act.out 2>act.err
}

GET() {
	REQUEST_METHOD="GET" && export REQUEST_METHOD &&
	run_backend "/repo.git/$1" &&
	sane_unset REQUEST_METHOD &&
	if ! grep "Status" act.out >act
	then
		printf "Status: 200 OK\r\n" >act
	fi
	printf "Status: $2\r\n" >exp &&
	test_cmp exp act
}

POST() {
	REQUEST_METHOD="POST" && export REQUEST_METHOD &&
	CONTENT_TYPE="application/x-$1-request" && export CONTENT_TYPE &&
	run_backend "/repo.git/$1" "$2" &&
	sane_unset REQUEST_METHOD &&
	sane_unset CONTENT_TYPE &&
	if ! grep "Status" act.out >act
	then
		printf "Status: 200 OK\r\n" >act
	fi
	printf "Status: $3\r\n" >exp &&
	test_cmp exp act
}

. "$TEST_DIRECTORY"/t556x_common

expect_aliased() {
	REQUEST_METHOD="GET" && export REQUEST_METHOD &&
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
