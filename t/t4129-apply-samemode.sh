#!/bin/sh

test_description='applying patch with mode bits'


TEST_PASSES_SANITIZE_LEAK=true
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
	git diff --stat -p >patch-1.txt &&
	sed "s/^\(new mode \).*/\1/" <patch-1.txt >patch-empty-mode.txt &&
	sed "s/^\(new mode \).*/\1garbage/" <patch-1.txt >patch-bogus-mode.txt
'

test_expect_success FILEMODE 'same mode (no index)' '
	git reset --hard &&
	chmod +x file &&
	git apply patch-0.txt &&
	test -x file
'

test_expect_success FILEMODE 'same mode (with index)' '
	git reset --hard &&
	chmod +x file &&
	git add file &&
	git apply --index patch-0.txt &&
	test -x file &&
	git diff --exit-code
'

test_expect_success FILEMODE 'same mode (index only)' '
	git reset --hard &&
	chmod +x file &&
	git add file &&
	git apply --cached patch-0.txt &&
	git ls-files -s file | grep "^100755"
'

test_expect_success FILEMODE 'mode update (no index)' '
	git reset --hard &&
	git apply patch-1.txt &&
	test -x file
'

test_expect_success FILEMODE 'mode update (with index)' '
	git reset --hard &&
	git apply --index patch-1.txt &&
	test -x file &&
	git diff --exit-code
'

test_expect_success FILEMODE 'mode update (index only)' '
	git reset --hard &&
	git apply --cached patch-1.txt &&
	git ls-files -s file | grep "^100755"
'

test_expect_success FILEMODE 'empty mode is rejected' '
	git reset --hard &&
	test_must_fail git apply patch-empty-mode.txt 2>err &&
	test_i18ngrep "invalid mode" err
'

test_expect_success FILEMODE 'bogus mode is rejected' '
	git reset --hard &&
	test_must_fail git apply patch-bogus-mode.txt 2>err &&
	test_i18ngrep "invalid mode" err
'

test_expect_success POSIXPERM 'do not use core.sharedRepository for working tree files' '
	git reset --hard &&
	test_config core.sharedRepository 0666 &&
	(
		# Remove a default ACL if possible.
		(setfacl -k . 2>/dev/null || true) &&
		umask 0077 &&

		# Test both files (f1) and leading dirs (d)
		mkdir d &&
		touch f1 d/f2 &&
		git add f1 d/f2 &&
		git diff --staged >patch-f1-and-f2.txt &&

		rm -rf d f1 &&
		git apply patch-f1-and-f2.txt &&

		echo "-rw-------" >f1_mode.expected &&
		echo "drwx------" >d_mode.expected &&
		test_modebits f1 >f1_mode.actual &&
		test_modebits d >d_mode.actual &&
		test_cmp f1_mode.expected f1_mode.actual &&
		test_cmp d_mode.expected d_mode.actual
	)
'

test_done
