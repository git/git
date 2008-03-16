#!/bin/sh

test_description='difference in submodules'

. ./test-lib.sh
. ../diff-lib.sh

_z40=0000000000000000000000000000000000000000
test_expect_success setup '
	test_tick &&
	test_create_repo sub &&
	(
		cd sub &&
		echo hello >world &&
		git add world &&
		git commit -m submodule
	) &&

	test_tick &&
	echo frotz >nitfol &&
	git add nitfol sub &&
	git commit -m superproject &&

	(
		cd sub &&
		echo goodbye >world &&
		git add world &&
		git commit -m "submodule #2"
	) &&

	set x $(
		cd sub &&
		git rev-list HEAD
	) &&
	echo ":160000 160000 $3 $_z40 M	sub" >expect
'

test_expect_success 'git diff --raw HEAD' '
	git diff --raw --abbrev=40 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'git diff-index --raw HEAD' '
	git diff-index --raw HEAD >actual.index &&
	test_cmp expect actual.index
'

test_expect_success 'git diff-files --raw' '
	git diff-files --raw >actual.files &&
	test_cmp expect actual.files
'

test_done
