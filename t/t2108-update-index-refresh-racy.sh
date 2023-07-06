#!/bin/sh

test_description='update-index refresh tests related to racy timestamps'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

reset_files () {
	echo content >file &&
	echo content >other &&
	test_set_magic_mtime file &&
	test_set_magic_mtime other
}

update_assert_changed () {
	test_set_magic_mtime .git/index &&
	test_might_fail git update-index "$1" &&
	! test_is_magic_mtime .git/index
}

test_expect_success 'setup' '
	reset_files &&
	# we are calling reset_files() a couple of times during tests;
	# test-tool chmtime does not change the ctime; to not weaken
	# or even break our tests, disable ctime-checks entirely
	git config core.trustctime false &&
	git add file other &&
	git commit -m "initial import"
'

test_expect_success '--refresh has no racy timestamps to fix' '
	reset_files &&
	# set the index time far enough to the future;
	# it must be at least 3 seconds for VFAT
	test_set_magic_mtime .git/index +60 &&
	git update-index --refresh &&
	test_is_magic_mtime .git/index +60
'

test_expect_success '--refresh should fix racy timestamp' '
	reset_files &&
	update_assert_changed --refresh
'

test_expect_success '--really-refresh should fix racy timestamp' '
	reset_files &&
	update_assert_changed --really-refresh
'

test_expect_success '--refresh should fix racy timestamp if other file needs update' '
	reset_files &&
	echo content2 >other &&
	test_set_magic_mtime other &&
	update_assert_changed --refresh
'

test_expect_success '--refresh should fix racy timestamp if racy file needs update' '
	reset_files &&
	echo content2 >file &&
	test_set_magic_mtime file &&
	update_assert_changed --refresh
'

test_done
