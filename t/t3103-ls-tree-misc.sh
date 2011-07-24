#!/bin/sh

test_description='
Miscellaneous tests for git ls-tree.

	      1. git ls-tree fails in presence of tree damage.

'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	touch a/one &&
	git add a/one &&
	git commit -m test
'

test_expect_failure 'ls-tree fails with non-zero exit code on broken tree' '
	rm -f .git/objects/5f/cffbd6e4c5c5b8d81f5e9314b20e338e3ffff5 &&
	test_must_fail git ls-tree -r HEAD
'

test_done
