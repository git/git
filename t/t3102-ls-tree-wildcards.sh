#!/bin/sh

test_description='ls-tree with(out) globs'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a aa "a[a]" &&
	touch a/one aa/two "a[a]/three" &&
	but add a/one aa/two "a[a]/three" &&
	but cummit -m test
'

test_expect_success 'ls-tree a[a] matches literally' '
	cat >expect <<-EOF &&
	100644 blob $EMPTY_BLOB	a[a]/three
	EOF
	but ls-tree -r HEAD "a[a]" >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-tree outside prefix' '
	cat >expect <<-EOF &&
	100644 blob $EMPTY_BLOB	../a[a]/three
	EOF
	( cd aa && but ls-tree -r HEAD "../a[a]" ) >actual &&
	test_cmp expect actual
'

test_expect_failure 'ls-tree does not yet support negated pathspec' '
	but ls-files ":(exclude)a" "a*" >expect &&
	but ls-tree --name-only -r HEAD ":(exclude)a" "a*" >actual &&
	test_cmp expect actual
'

test_done
