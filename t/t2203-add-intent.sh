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

test_expect_success 'i-t-a entry is simply ignored' '
	test_tick &&
	git commit -a -m initial &&
	git reset --hard &&

	echo xyzzy >rezrov &&
	echo frotz >nitfol &&
	git add rezrov &&
	git add -N nitfol &&
	git commit -m second &&
	test $(git ls-tree HEAD -- nitfol | wc -l) = 0 &&
	test $(git diff --name-only HEAD -- nitfol | wc -l) = 1
'

test_expect_success 'can commit with an unrelated i-t-a entry in index' '
	git reset --hard &&
	echo bozbar >rezrov &&
	echo frotz >nitfol &&
	git add rezrov &&
	git add -N nitfol &&
	git commit -m partial rezrov
'

test_expect_success 'can "commit -a" with an i-t-a entry' '
	git reset --hard &&
	: >nitfol &&
	git add -N nitfol &&
	git commit -a -m all
'

test_expect_success 'cache-tree invalidates i-t-a paths' '
	git reset --hard &&
	mkdir dir &&
	: >dir/foo &&
	git add dir/foo &&
	git commit -m foo &&

	: >dir/bar &&
	git add -N dir/bar &&
	git diff --cached --name-only >actual &&
	echo dir/bar >expect &&
	test_cmp expect actual &&

	git write-tree >/dev/null &&

	git diff --cached --name-only >actual &&
	echo dir/bar >expect &&
	test_cmp expect actual
'

test_done

