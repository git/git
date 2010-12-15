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

test_expect_success POSIXPERM,SANITY 'write-tree should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git write-tree
'

test_expect_success POSIXPERM,SANITY 'commit should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git commit -m second
'

test_expect_success POSIXPERM,SANITY 'update-index should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	echo 6O >file &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git update-index file
'

test_expect_success POSIXPERM,SANITY 'add should notice unwritable repository' '
	test_when_finished "chmod 775 .git/objects .git/objects/??" &&
	echo b >file &&
	chmod a-w .git/objects .git/objects/?? &&
	test_must_fail git add file
'

test_done
