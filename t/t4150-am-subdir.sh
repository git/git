#!/bin/sh

test_description='git am running from a subdirectory'

. ./test-lib.sh

test_expect_success setup '
	echo hello >world &&
	git add world &&
	test_tick &&
	git commit -m initial &&
	git tag initial &&
	echo goodbye >world &&
	git add world &&
	test_tick &&
	git commit -m second &&
	git format-patch --stdout HEAD^ >patchfile &&
	: >expect
'

test_expect_success 'am regularly from stdin' '
	git checkout initial &&
	git am <patchfile &&
	git diff master >actual &&
	test_cmp expect actual
'

test_expect_success 'am regularly from file' '
	git checkout initial &&
	git am patchfile &&
	git diff master >actual &&
	test_cmp expect actual
'

test_expect_success 'am regularly from stdin in subdirectory' '
	rm -fr subdir &&
	git checkout initial &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am <../patchfile
	) &&
	git diff master>actual &&
	test_cmp expect actual
'

test_expect_success 'am regularly from file in subdirectory' '
	rm -fr subdir &&
	git checkout initial &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am ../patchfile
	) &&
	git diff master >actual &&
	test_cmp expect actual
'

test_expect_success 'am regularly from file in subdirectory with full path' '
	rm -fr subdir &&
	git checkout initial &&
	P=$(pwd) &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am "$P/patchfile"
	) &&
	git diff master >actual &&
	test_cmp expect actual
'

test_done
