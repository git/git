#!/bin/sh

test_description='
Miscellaneous tests for but ls-tree.

	      1. but ls-tree fails in presence of tree damage.

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	touch a/one &&
	but add a/one &&
	but cummit -m test
'

test_expect_success 'ls-tree fails with non-zero exit code on broken tree' '
	tree=$(but rev-parse HEAD:a) &&
	rm -f .but/objects/$(echo $tree | sed -e "s,^\(..\),\1/,") &&
	test_must_fail but ls-tree -r HEAD
'

for opts in \
	"--long --name-only" \
	"--name-only --name-status" \
	"--name-status --object-only" \
	"--object-only --long"
do
	test_expect_success "usage: incompatible options: $opts" '
		test_expect_code 129 but ls-tree $opts $tree
	'

	one_opt=$(echo "$opts" | cut -d' '  -f1)
	test_expect_success "usage: incompatible options: $one_opt and --format" '
		test_expect_code 129 but ls-tree $one_opt --format=fmt $tree
	'
done
test_done
