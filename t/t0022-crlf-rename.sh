#!/bin/sh

test_description='ignore CR in CRLF sequence while computing similiarity'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	cat "$TEST_DIRECTORY"/t0022-crlf-rename.sh >sample &&
	but add sample &&

	test_tick &&
	but cummit -m Initial &&

	append_cr <"$TEST_DIRECTORY"/t0022-crlf-rename.sh >elpmas &&
	but add elpmas &&
	rm -f sample &&

	test_tick &&
	but cummit -a -m Second

'

test_expect_success 'diff -M' '

	but diff-tree -M -r --name-status HEAD^ HEAD >tmp &&
	sed -e "s/R[0-9]*/RNUM/" tmp >actual &&
	echo "RNUM	sample	elpmas" >expect &&
	test_cmp expect actual

'

test_done
