#!/bin/sh

test_description='Examples from the git-notes man page

Make sure the manual is not full of lies.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit A &&
	test_commit B &&
	test_commit C
'

test_expect_success 'example 1: notes to add an Acked-by line' '
	cat <<-\EOF >expect &&
	    B

	Notes:
	    Acked-by: A C Ker <acker@example.com>
	EOF
	git notes add -m "Acked-by: A C Ker <acker@example.com>" B &&
	git show -s B^{commit} >log &&
	tail -n 4 log >actual &&
	test_cmp expect actual
'

test_expect_success 'example 2: binary notes' '
	cp "$TEST_DIRECTORY"/test4012.png .
	git checkout B &&
	blob=$(git hash-object -w test4012.png) &&
	git notes --ref=logo add -C "$blob" &&
	git notes --ref=logo copy B C &&
	git notes --ref=logo show C >actual &&
	test_cmp test4012.png actual
'

test_done
