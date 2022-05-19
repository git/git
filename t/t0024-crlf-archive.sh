#!/bin/sh

test_description='respect crlf in but archive'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	but config core.autocrlf true &&

	printf "CRLF line ending\r\nAnd another\r\n" > sample &&
	but add sample &&

	test_tick &&
	but cummit -m Initial

'

test_expect_success 'tar archive' '

	but archive --format=tar HEAD |
	( mkdir untarred && cd untarred && "$TAR" -xf - ) &&

	test_cmp sample untarred/sample

'

test_expect_success UNZIP 'zip archive' '

	but archive --format=zip HEAD >test.zip &&

	( mkdir unzipped && cd unzipped && "$BUT_UNZIP" ../test.zip ) &&

	test_cmp sample unzipped/sample

'

test_done
