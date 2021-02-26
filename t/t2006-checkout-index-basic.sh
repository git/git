#!/bin/sh

test_description='basic checkout-index tests
'

. ./test-lib.sh

test_expect_success 'checkout-index --gobbledegook' '
	test_expect_code 129 git checkout-index --gobbledegook 2>err &&
	test_i18ngrep "[Uu]sage" err
'

test_expect_success 'checkout-index -h in broken repository' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		>.git/index &&
		test_expect_code 129 git checkout-index -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'checkout-index reports errors (cmdline)' '
	test_must_fail git checkout-index -- does-not-exist 2>stderr &&
	test_i18ngrep not.in.the.cache stderr
'

test_expect_success 'checkout-index reports errors (stdin)' '
	echo does-not-exist |
	test_must_fail git checkout-index --stdin 2>stderr &&
	test_i18ngrep not.in.the.cache stderr
'

test_expect_success 'checkout-index --temp correctly reports error on missing blobs' '
	test_when_finished git reset --hard &&
	missing_blob=$(echo "no such blob here" | git hash-object --stdin) &&
	cat >objs <<-EOF &&
	100644 $missing_blob	file
	120000 $missing_blob	symlink
	EOF
	git update-index --index-info <objs &&

	test_must_fail git checkout-index --temp symlink file 2>stderr &&
	test_i18ngrep "unable to read sha1 file of file ($missing_blob)" stderr &&
	test_i18ngrep "unable to read sha1 file of symlink ($missing_blob)" stderr
'

test_expect_success 'checkout-index --temp correctly reports error for submodules' '
	git init sub &&
	test_commit -C sub file &&
	git submodule add ./sub &&
	git commit -m sub &&
	test_must_fail git checkout-index --temp sub 2>stderr &&
	test_i18ngrep "cannot create temporary submodule sub" stderr
'

test_done
