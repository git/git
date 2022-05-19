#!/bin/sh

test_description='setup taking and sanitizing funny paths'

. ./test-lib.sh

test_expect_success setup '

	mkdir -p a/b/c a/e &&
	D=$(pwd) &&
	>a/b/c/d &&
	>a/e/f

'

test_expect_success 'but add (absolute)' '

	but add "$D/a/b/c/d" &&
	but ls-files >current &&
	echo a/b/c/d >expect &&
	test_cmp expect current

'


test_expect_success 'but add (funny relative)' '

	rm -f .but/index &&
	(
		cd a/b &&
		but add "../e/./f"
	) &&
	but ls-files >current &&
	echo a/e/f >expect &&
	test_cmp expect current

'

test_expect_success 'but rm (absolute)' '

	rm -f .but/index &&
	but add a &&
	but rm -f --cached "$D/a/b/c/d" &&
	but ls-files >current &&
	echo a/e/f >expect &&
	test_cmp expect current

'

test_expect_success 'but rm (funny relative)' '

	rm -f .but/index &&
	but add a &&
	(
		cd a/b &&
		but rm -f --cached "../e/./f"
	) &&
	but ls-files >current &&
	echo a/b/c/d >expect &&
	test_cmp expect current

'

test_expect_success 'but ls-files (absolute)' '

	rm -f .but/index &&
	but add a &&
	but ls-files "$D/a/e/../b" >current &&
	echo a/b/c/d >expect &&
	test_cmp expect current

'

test_expect_success 'but ls-files (relative #1)' '

	rm -f .but/index &&
	but add a &&
	(
		cd a/b &&
		but ls-files "../b/c"
	)  >current &&
	echo c/d >expect &&
	test_cmp expect current

'

test_expect_success 'but ls-files (relative #2)' '

	rm -f .but/index &&
	but add a &&
	(
		cd a/b &&
		but ls-files --full-name "../e/f"
	)  >current &&
	echo a/e/f >expect &&
	test_cmp expect current

'

test_expect_success 'but ls-files (relative #3)' '

	rm -f .but/index &&
	but add a &&
	(
		cd a/b &&
		but ls-files "../e/f"
	)  >current &&
	echo ../e/f >expect &&
	test_cmp expect current

'

test_expect_success 'cummit using absolute path names' '
	but cummit -m "foo" &&
	echo aa >>a/b/c/d &&
	but cummit -m "aa" "$(pwd)/a/b/c/d"
'

test_expect_success 'log using absolute path names' '
	echo bb >>a/b/c/d &&
	but cummit -m "bb" "$(pwd)/a/b/c/d" &&

	but log a/b/c/d >f1.txt &&
	but log "$(pwd)/a/b/c/d" >f2.txt &&
	test_cmp f1.txt f2.txt
'

test_expect_success 'blame using absolute path names' '
	but blame a/b/c/d >f1.txt &&
	but blame "$(pwd)/a/b/c/d" >f2.txt &&
	test_cmp f1.txt f2.txt
'

test_expect_success 'setup deeper work tree' '
	test_create_repo tester
'

test_expect_success 'add a directory outside the work tree' '(
	cd tester &&
	d1="$(cd .. && pwd)" &&
	test_must_fail but add "$d1"
)'


test_expect_success 'add a file outside the work tree, nasty case 1' '(
	cd tester &&
	f="$(pwd)x" &&
	echo "$f" &&
	touch "$f" &&
	test_must_fail but add "$f"
)'

test_expect_success 'add a file outside the work tree, nasty case 2' '(
	cd tester &&
	f="$(pwd | sed "s/.$//")x" &&
	echo "$f" &&
	touch "$f" &&
	test_must_fail but add "$f"
)'

test_done
