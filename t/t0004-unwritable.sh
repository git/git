#!/bin/sh

test_description='detect unwritable repository and fail correctly'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo >file &&
	git add file

'

test_expect_success POSIXPERM 'write-tree should notice unwritable repository' '

	(
		chmod a-w .git/objects .git/objects/?? &&
		test_must_fail git write-tree
	)
	status=$?
	chmod 775 .git/objects .git/objects/??
	(exit $status)

'

test_expect_success POSIXPERM 'commit should notice unwritable repository' '

	(
		chmod a-w .git/objects .git/objects/?? &&
		test_must_fail git commit -m second
	)
	status=$?
	chmod 775 .git/objects .git/objects/??
	(exit $status)

'

test_expect_success POSIXPERM 'update-index should notice unwritable repository' '

	(
		echo 6O >file &&
		chmod a-w .git/objects .git/objects/?? &&
		test_must_fail git update-index file
	)
	status=$?
	chmod 775 .git/objects .git/objects/??
	(exit $status)

'

test_expect_success POSIXPERM 'add should notice unwritable repository' '

	(
		echo b >file &&
		chmod a-w .git/objects .git/objects/?? &&
		test_must_fail git add file
	)
	status=$?
	chmod 775 .git/objects .git/objects/??
	(exit $status)

'

test_done
