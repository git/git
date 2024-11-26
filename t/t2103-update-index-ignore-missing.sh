#!/bin/sh

test_description='update-index with options'

. ./test-lib.sh

test_expect_success basics '
	>one &&
	>two &&
	>three &&

	# need --add when adding
	test_must_fail git update-index one &&
	test -z "$(git ls-files)" &&
	git update-index --add one &&
	test zone = "z$(git ls-files)" &&

	# update-index is atomic
	echo 1 >one &&
	test_must_fail git update-index one two &&
	echo "M	one" >expect &&
	git diff-files --name-status >actual &&
	test_cmp expect actual &&

	git update-index --add one two three &&
	test_write_lines one three two >expect &&
	git ls-files >actual &&
	test_cmp expect actual &&

	test_tick &&
	(
		test_create_repo xyzzy &&
		cd xyzzy &&
		>file &&
		git add file &&
		git commit -m "sub initial"
	) &&
	git add xyzzy &&

	test_tick &&
	git commit -m initial &&
	git tag initial
'

test_expect_success '--ignore-missing --refresh' '
	git reset --hard initial &&
	echo 2 >one &&
	test_must_fail git update-index --refresh &&
	echo 1 >one &&
	git update-index --refresh &&
	rm -f two &&
	test_must_fail git update-index --refresh &&
	git update-index --ignore-missing --refresh

'

test_expect_success '--unmerged --refresh' '
	git reset --hard initial &&
	info=$(git ls-files -s one | sed -e "s/ 0	/ 1	/") &&
	git rm --cached one &&
	echo "$info" | git update-index --index-info &&
	test_must_fail git update-index --refresh &&
	git update-index --unmerged --refresh &&
	echo 2 >two &&
	test_must_fail git update-index --unmerged --refresh >actual &&
	grep two actual &&
	! grep one actual &&
	! grep three actual
'

test_expect_success '--ignore-submodules --refresh (1)' '
	git reset --hard initial &&
	rm -f two &&
	test_must_fail git update-index --ignore-submodules --refresh
'

test_expect_success '--ignore-submodules --refresh (2)' '
	git reset --hard initial &&
	test_tick &&
	(
		cd xyzzy &&
		git commit -m "sub second" --allow-empty
	) &&
	test_must_fail git update-index --refresh &&
	test_must_fail git update-index --ignore-missing --refresh &&
	git update-index --ignore-submodules --refresh
'

test_done
