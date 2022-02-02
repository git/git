#!/bin/sh

test_description='git ls-tree objects handling.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit A &&
	test_commit B &&
	mkdir -p C &&
	test_commit C/D.txt &&
	find *.txt path* \( -type f -o -type l \) -print |
	xargs git update-index --add &&
	tree=$(git write-tree) &&
	echo $tree
'

test_expect_success 'usage: --object-only' '
	git ls-tree --object-only $tree >current &&
	git ls-tree $tree >result &&
	cut -f1 result | cut -d " " -f3 >expected &&
	test_cmp current expected
'

test_expect_success 'usage: --object-only with -r' '
	git ls-tree --object-only -r $tree >current &&
	git ls-tree -r $tree >result &&
	cut -f1 result | cut -d " " -f3 >expected &&
	test_cmp current expected
'

test_expect_success 'usage: --object-only with --abbrev' '
	git ls-tree --object-only --abbrev=6 $tree >current &&
	git ls-tree --abbrev=6 $tree >result &&
	cut -f1 result | cut -d " " -f3 >expected &&
	test_cmp current expected
'

test_expect_success 'usage: incompatible options: --name-only with --object-only' '
	test_expect_code 129 git ls-tree --object-only --name-only $tree
'

test_expect_success 'usage: incompatible options: --name-status with --object-only' '
	test_expect_code 129 git ls-tree --object-only --name-status $tree
'

test_expect_success 'usage: incompatible options: --long with --object-only' '
	test_expect_code 129 git ls-tree --object-only --long $tree
'

test_done
