#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='but apply symlinks and partial files

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	test_ln_s_add path1/path2/path3/path4/path5 link1 &&
	but cummit -m initial &&

	but branch side &&

	rm -f link? &&

	test_ln_s_add htap6 link1 &&
	but cummit -m second &&

	but diff-tree -p HEAD^ HEAD >patch  &&
	but apply --stat --summary patch

'

test_expect_success SYMLINKS 'apply symlink patch' '

	but checkout side &&
	but apply patch &&
	but diff-files -p >patched &&
	test_cmp patch patched

'

test_expect_success 'apply --index symlink patch' '

	but checkout -f side &&
	but apply --index patch &&
	but diff-index --cached -p HEAD >patched &&
	test_cmp patch patched

'

test_done
