#!/bin/sh

test_description='respect crlf in git archive'

. ./test-lib.sh
UNZIP=${UNZIP:-unzip}

test_expect_success setup '

	git config core.autocrlf true

	printf "CRLF line ending\r\nAnd another\r\n" > sample &&
	git add sample &&

	test_tick &&
	git commit -m Initial

'

test_expect_success 'tar archive' '

	git archive --format=tar HEAD |
	( mkdir untarred && cd untarred && "$TAR" -xf - )

	test_cmp sample untarred/sample

'

"$UNZIP" -v >/dev/null 2>&1
if [ $? -eq 127 ]; then
	say "Skipping ZIP test, because unzip was not found"
else
	test_set_prereq UNZIP
fi

test_expect_success UNZIP 'zip archive' '

	git archive --format=zip HEAD >test.zip &&

	( mkdir unzipped && cd unzipped && unzip ../test.zip ) &&

	test_cmp sample unzipped/sample

'

test_done
