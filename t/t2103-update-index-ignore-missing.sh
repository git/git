#!/bin/sh

test_description='update-index with options'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success basics '
	>one &&
	>two &&
	>three &&

	# need --add when adding
	test_must_fail but update-index one &&
	test -z "$(but ls-files)" &&
	but update-index --add one &&
	test zone = "z$(but ls-files)" &&

	# update-index is atomic
	echo 1 >one &&
	test_must_fail but update-index one two &&
	echo "M	one" >expect &&
	but diff-files --name-status >actual &&
	test_cmp expect actual &&

	but update-index --add one two three &&
	test_write_lines one three two >expect &&
	but ls-files >actual &&
	test_cmp expect actual &&

	test_tick &&
	(
		test_create_repo xyzzy &&
		cd xyzzy &&
		>file &&
		but add file &&
		but cummit -m "sub initial"
	) &&
	but add xyzzy &&

	test_tick &&
	but cummit -m initial &&
	but tag initial
'

test_expect_success '--ignore-missing --refresh' '
	but reset --hard initial &&
	echo 2 >one &&
	test_must_fail but update-index --refresh &&
	echo 1 >one &&
	but update-index --refresh &&
	rm -f two &&
	test_must_fail but update-index --refresh &&
	but update-index --ignore-missing --refresh

'

test_expect_success '--unmerged --refresh' '
	but reset --hard initial &&
	info=$(but ls-files -s one | sed -e "s/ 0	/ 1	/") &&
	but rm --cached one &&
	echo "$info" | but update-index --index-info &&
	test_must_fail but update-index --refresh &&
	but update-index --unmerged --refresh &&
	echo 2 >two &&
	test_must_fail but update-index --unmerged --refresh >actual &&
	grep two actual &&
	! grep one actual &&
	! grep three actual
'

test_expect_success '--ignore-submodules --refresh (1)' '
	but reset --hard initial &&
	rm -f two &&
	test_must_fail but update-index --ignore-submodules --refresh
'

test_expect_success '--ignore-submodules --refresh (2)' '
	but reset --hard initial &&
	test_tick &&
	(
		cd xyzzy &&
		but cummit -m "sub second" --allow-empty
	) &&
	test_must_fail but update-index --refresh &&
	test_must_fail but update-index --ignore-missing --refresh &&
	but update-index --ignore-submodules --refresh
'

test_done
