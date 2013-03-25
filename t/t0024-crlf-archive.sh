#!/bin/sh

test_description='respect crlf in git archive'

. ./test-lib.sh

test_expect_success setup '

	git config core.autocrlf true &&

	printf "CRLF line ending\r\nAnd another\r\n" > sample &&
	git add sample &&

	test_tick &&
	git commit -m Initial

'

test_expect_success 'tar archive' '

	git archive --format=tar HEAD |
	( mkdir untarred && cd untarred && "$TAR" -xf - ) &&

	test_cmp sample untarred/sample

'

test_expect_success UNZIP 'zip archive' '

	git archive --format=zip HEAD >test.zip &&

	( mkdir unzipped && cd unzipped && "$GIT_UNZIP" ../test.zip ) &&

	test_cmp sample unzipped/sample

'

test_done
