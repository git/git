#!/bin/sh

test_description='applying patch with mode bits'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo original >file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but tag initial &&
	echo modified >file &&
	but diff --stat -p >patch-0.txt &&
	chmod +x file &&
	but diff --stat -p >patch-1.txt &&
	sed "s/^\(new mode \).*/\1/" <patch-1.txt >patch-empty-mode.txt &&
	sed "s/^\(new mode \).*/\1garbage/" <patch-1.txt >patch-bogus-mode.txt
'

test_expect_success FILEMODE 'same mode (no index)' '
	but reset --hard &&
	chmod +x file &&
	but apply patch-0.txt &&
	test -x file
'

test_expect_success FILEMODE 'same mode (with index)' '
	but reset --hard &&
	chmod +x file &&
	but add file &&
	but apply --index patch-0.txt &&
	test -x file &&
	but diff --exit-code
'

test_expect_success FILEMODE 'same mode (index only)' '
	but reset --hard &&
	chmod +x file &&
	but add file &&
	but apply --cached patch-0.txt &&
	but ls-files -s file | grep "^100755"
'

test_expect_success FILEMODE 'mode update (no index)' '
	but reset --hard &&
	but apply patch-1.txt &&
	test -x file
'

test_expect_success FILEMODE 'mode update (with index)' '
	but reset --hard &&
	but apply --index patch-1.txt &&
	test -x file &&
	but diff --exit-code
'

test_expect_success FILEMODE 'mode update (index only)' '
	but reset --hard &&
	but apply --cached patch-1.txt &&
	but ls-files -s file | grep "^100755"
'

test_expect_success FILEMODE 'empty mode is rejected' '
	but reset --hard &&
	test_must_fail but apply patch-empty-mode.txt 2>err &&
	test_i18ngrep "invalid mode" err
'

test_expect_success FILEMODE 'bogus mode is rejected' '
	but reset --hard &&
	test_must_fail but apply patch-bogus-mode.txt 2>err &&
	test_i18ngrep "invalid mode" err
'

test_expect_success POSIXPERM 'do not use core.sharedRepository for working tree files' '
	but reset --hard &&
	test_config core.sharedRepository 0666 &&
	(
		# Remove a default ACL if possible.
		(setfacl -k . 2>/dev/null || true) &&
		umask 0077 &&

		# Test both files (f1) and leading dirs (d)
		mkdir d &&
		touch f1 d/f2 &&
		but add f1 d/f2 &&
		but diff --staged >patch-f1-and-f2.txt &&

		rm -rf d f1 &&
		but apply patch-f1-and-f2.txt &&

		echo "-rw-------" >f1_mode.expected &&
		echo "drwx------" >d_mode.expected &&
		test_modebits f1 >f1_mode.actual &&
		test_modebits d >d_mode.actual &&
		test_cmp f1_mode.expected f1_mode.actual &&
		test_cmp d_mode.expected d_mode.actual
	)
'

test_done
