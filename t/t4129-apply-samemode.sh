#!/bin/sh

test_description='applying patch with mode bits'

. ./test-lib.sh

test_expect_success setup '
	echo original >file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag initial &&
	echo modified >file &&
	git diff --stat -p >patch-0.txt &&
	chmod +x file &&
	git diff --stat -p >patch-1.txt
'

test "$(git config --bool core.filemode)" = false &&
say "executable bit not honored - skipping" &&
test_done &&
exit

test_expect_success 'same mode (no index)' '
	git reset --hard &&
	chmod +x file &&
	git apply patch-0.txt &&
	test -x file
'

test_expect_success 'same mode (with index)' '
	git reset --hard &&
	chmod +x file &&
	git add file &&
	git apply --index patch-0.txt &&
	test -x file &&
	git diff --exit-code
'

test_expect_success 'same mode (index only)' '
	git reset --hard &&
	chmod +x file &&
	git add file &&
	git apply --cached patch-0.txt &&
	git ls-files -s file | grep "^100755"
'

test_expect_success 'mode update (no index)' '
	git reset --hard &&
	git apply patch-1.txt &&
	test -x file
'

test_expect_success 'mode update (with index)' '
	git reset --hard &&
	git apply --index patch-1.txt &&
	test -x file &&
	git diff --exit-code
'

test_expect_success 'mode update (index only)' '
	git reset --hard &&
	git apply --cached patch-1.txt &&
	git ls-files -s file | grep "^100755"
'

test_done
