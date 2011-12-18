#!/bin/sh

test_description='ignore CR in CRLF sequence while computing similiarity'

. ./test-lib.sh

test_expect_success setup '

	cat "$TEST_DIRECTORY"/t0022-crlf-rename.sh >sample &&
	git add sample &&

	test_tick &&
	git commit -m Initial &&

	append_cr <"$TEST_DIRECTORY"/t0022-crlf-rename.sh >elpmas &&
	git add elpmas &&
	rm -f sample &&

	test_tick &&
	git commit -a -m Second

'

test_expect_success 'diff -M' '

	git diff-tree -M -r --name-status HEAD^ HEAD |
	sed -e "s/R[0-9]*/RNUM/" >actual &&
	echo "RNUM	sample	elpmas" >expect &&
	test_cmp expect actual

'

test_done
