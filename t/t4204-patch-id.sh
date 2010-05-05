#!/bin/sh

test_description='git patch-id'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial foo a &&
	test_commit first foo b &&
	git checkout -b same HEAD^ &&
	test_commit same-msg foo b &&
	git checkout -b notsame HEAD^ &&
	test_commit notsame-msg foo c
'

test_expect_success 'patch-id output is well-formed' '
	git log -p -1 | git patch-id > output &&
	grep "^[a-f0-9]\{40\} $(git rev-parse HEAD)$" output
'

calc_patch_id () {
	git patch-id |
		sed "s# .*##" > patch-id_"$1"
}

get_patch_id () {
	git log -p -1 "$1" | git patch-id |
		sed "s# .*##" > patch-id_"$1"
}

test_expect_success 'patch-id detects equality' '
	get_patch_id master &&
	get_patch_id same &&
	test_cmp patch-id_master patch-id_same
'

test_expect_success 'patch-id detects inequality' '
	get_patch_id master &&
	get_patch_id notsame &&
	! test_cmp patch-id_master patch-id_notsame
'

test_expect_success 'patch-id supports git-format-patch output' '
	get_patch_id master &&
	git checkout same &&
	git format-patch -1 --stdout | calc_patch_id same &&
	test_cmp patch-id_master patch-id_same &&
	set `git format-patch -1 --stdout | git patch-id` &&
	test "$2" = `git rev-parse HEAD`
'

test_expect_success 'whitespace is irrelevant in footer' '
	get_patch_id master &&
	git checkout same &&
	git format-patch -1 --stdout | sed "s/ \$//" | calc_patch_id same &&
	test_cmp patch-id_master patch-id_same
'

test_expect_success 'patch-id supports git-format-patch MIME output' '
	get_patch_id master &&
	git checkout same &&
	git format-patch -1 --attach --stdout | calc_patch_id same &&
	test_cmp patch-id_master patch-id_same
'

test_done
