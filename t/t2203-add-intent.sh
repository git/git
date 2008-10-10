#!/bin/sh

test_description='Intent to add'

. ./test-lib.sh

test_expect_success 'intent to add' '
	echo hello >file &&
	echo hello >elif &&
	git add -N file &&
	git add elif
'

test_expect_success 'check result of "add -N"' '
	git ls-files -s file >actual &&
	empty=$(git hash-object --stdin </dev/null) &&
	echo "100644 $empty 0	file" >expect &&
	test_cmp expect actual
'

test_expect_success 'intent to add is just an ordinary empty blob' '
	git add -u &&
	git ls-files -s file >actual &&
	git ls-files -s elif | sed -e "s/elif/file/" >expect &&
	test_cmp expect actual
'

test_expect_success 'intent to add does not clobber existing paths' '
	git add -N file elif &&
	empty=$(git hash-object --stdin </dev/null) &&
	git ls-files -s >actual &&
	! grep "$empty" actual
'

test_done

