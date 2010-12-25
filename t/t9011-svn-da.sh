#!/bin/sh

test_description='test parsing of svndiff0 files

Using the "test-svn-fe -d" helper, check that svn-fe correctly
interprets deltas using various facilities (some from the spec,
some only learned from practice).
'
. ./test-lib.sh

>empty
printf foo >preimage

test_expect_success 'reject empty delta' '
	test_must_fail test-svn-fe -d preimage empty 0
'

test_expect_success 'delta can empty file' '
	printf "SVNQ" | q_to_nul >clear.delta &&
	test-svn-fe -d preimage clear.delta 4 >actual &&
	test_cmp empty actual
'

test_expect_success 'reject svndiff2' '
	printf "SVN\002" >bad.filetype &&
	test_must_fail test-svn-fe -d preimage bad.filetype 4
'

test_expect_failure 'one-window empty delta' '
	printf "SVNQ%s" "QQQQQ" | q_to_nul >clear.onewindow &&
	test-svn-fe -d preimage clear.onewindow 9 >actual &&
	test_cmp empty actual
'

test_done
