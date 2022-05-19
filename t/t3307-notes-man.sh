#!/bin/sh

test_description='Examples from the but-notes man page

Make sure the manual is not full of lies.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit A &&
	test_cummit B &&
	test_cummit C
'

test_expect_success 'example 1: notes to add an Acked-by line' '
	cat <<-\EOF >expect &&
	    B

	Notes:
	    Acked-by: A C Ker <acker@example.com>
	EOF
	but notes add -m "Acked-by: A C Ker <acker@example.com>" B &&
	but show -s B^{cummit} >log &&
	tail -n 4 log >actual &&
	test_cmp expect actual
'

test_expect_success 'example 2: binary notes' '
	cp "$TEST_DIRECTORY"/test-binary-1.png . &&
	but checkout B &&
	blob=$(but hash-object -w test-binary-1.png) &&
	but notes --ref=logo add -C "$blob" &&
	but notes --ref=logo copy B C &&
	but notes --ref=logo show C >actual &&
	test_cmp test-binary-1.png actual
'

test_done
