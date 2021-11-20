#!/bin/sh

test_description='
Miscellaneous tests for git ls-tree.

	      1. git ls-tree fails in presence of tree damage.

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	touch a/one &&
	git add a/one &&
	git commit -m test
'

test_expect_success 'ls-tree fails with non-zero exit code on broken tree' '
	tree=$(git rev-parse HEAD:a) &&
	rm -f .git/objects/$(echo $tree | sed -e "s,^\(..\),\1/,") &&
	test_must_fail git ls-tree -r HEAD
'

test_done
