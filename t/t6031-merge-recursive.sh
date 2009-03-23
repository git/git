#!/bin/sh

test_description='merge-recursive: handle file mode'
. ./test-lib.sh

if ! test "$(git config --bool core.filemode)" = false
then
	test_set_prereq FILEMODE
fi

test_expect_success 'mode change in one branch: keep changed version' '
	: >file1 &&
	git add file1 &&
	git commit -m initial &&
	git checkout -b a1 master &&
	: >dummy &&
	git add dummy &&
	git commit -m a &&
	git checkout -b b1 master &&
	test_chmod +x file1 &&
	git commit -m b1 &&
	git checkout a1 &&
	git merge-recursive master -- a1 b1 &&
	git ls-files -s file1 | grep ^100755
'

test_expect_success FILEMODE 'verify executable bit on file' '
	test -x file1
'

test_expect_success 'mode change in both branches: expect conflict' '
	git reset --hard HEAD &&
	git checkout -b a2 master &&
	: >file2 &&
	H=$(git hash-object file2) &&
	test_chmod +x file2 &&
	git commit -m a2 &&
	git checkout -b b2 master &&
	: >file2 &&
	git add file2 &&
	git commit -m b2 &&
	git checkout a2 &&
	(
		git merge-recursive master -- a2 b2
		test $? = 1
	) &&
	git ls-files -u >actual &&
	(
		echo "100755 $H 2	file2"
		echo "100644 $H 3	file2"
	) >expect &&
	test_cmp actual expect &&
	git ls-files -s file2 | grep ^100755
'

test_expect_success FILEMODE 'verify executable bit on file' '
	test -x file2
'

test_done
