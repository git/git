#!/bin/sh

test_description='git annotate'
. ./test-lib.sh

PROG='git annotate'
. "$TEST_DIRECTORY"/annotate-tests.sh

test_expect_success 'Annotating an old revision works' '
	git annotate file master >result &&
	awk "{ print \$3; }" <result >authors &&
	test 2 = $(grep A <authors | wc -l) &&
	test 2 = $(grep B <authors | wc -l)
'

test_done
