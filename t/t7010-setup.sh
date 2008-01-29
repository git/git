#!/bin/sh

test_description='setup taking and sanitizing funny paths'

. ./test-lib.sh

test_expect_success setup '

	mkdir -p a/b/c a/e &&
	D=$(pwd) &&
	>a/b/c/d &&
	>a/e/f

'

test_expect_success 'git add (absolute)' '

	git add "$D/a/b/c/d" &&
	git ls-files >current &&
	echo a/b/c/d >expect &&
	diff -u expect current

'


test_expect_success 'git add (funny relative)' '

	rm -f .git/index &&
	(
		cd a/b &&
		git add "../e/./f"
	) &&
	git ls-files >current &&
	echo a/e/f >expect &&
	diff -u expect current

'

test_expect_success 'git rm (absolute)' '

	rm -f .git/index &&
	git add a &&
	git rm -f --cached "$D/a/b/c/d" &&
	git ls-files >current &&
	echo a/e/f >expect &&
	diff -u expect current

'

test_expect_success 'git rm (funny relative)' '

	rm -f .git/index &&
	git add a &&
	(
		cd a/b &&
		git rm -f --cached "../e/./f"
	) &&
	git ls-files >current &&
	echo a/b/c/d >expect &&
	diff -u expect current

'

test_expect_success 'git ls-files (absolute)' '

	rm -f .git/index &&
	git add a &&
	git ls-files "$D/a/e/../b" >current &&
	echo a/b/c/d >expect &&
	diff -u expect current

'

test_expect_success 'git ls-files (relative #1)' '

	rm -f .git/index &&
	git add a &&
	(
		cd a/b &&
		git ls-files "../b/c"
	)  >current &&
	echo c/d >expect &&
	diff -u expect current

'

test_expect_success 'git ls-files (relative #2)' '

	rm -f .git/index &&
	git add a &&
	(
		cd a/b &&
		git ls-files --full-name "../e/f"
	)  >current &&
	echo a/e/f >expect &&
	diff -u expect current

'

test_expect_success 'git ls-files (relative #3)' '

	rm -f .git/index &&
	git add a &&
	(
		cd a/b &&
		if git ls-files "../e/f"
		then
			echo Gaah, should have failed
			exit 1
		else
			: happy
		fi
	)

'

test_done
