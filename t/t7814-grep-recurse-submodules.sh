#!/bin/sh

test_description='Test grep recurse-submodules feature

This test verifies the recurse-submodules feature correctly greps across
submodules.
'

. ./test-lib.sh

test_expect_success 'setup directory structure and submodule' '
	echo "foobar" >a &&
	mkdir b &&
	echo "bar" >b/b &&
	git add a b &&
	git commit -m "add a and b" &&
	git init submodule &&
	echo "foobar" >submodule/a &&
	git -C submodule add a &&
	git -C submodule commit -m "add a" &&
	git submodule add ./submodule &&
	git commit -m "added submodule"
'

test_expect_success 'grep correctly finds patterns in a submodule' '
	cat >expect <<-\EOF &&
	a:foobar
	b/b:bar
	submodule/a:foobar
	EOF

	git grep -e "bar" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and basic pathspecs' '
	cat >expect <<-\EOF &&
	submodule/a:foobar
	EOF

	git grep -e. --recurse-submodules -- submodule >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and nested submodules' '
	git init submodule/sub &&
	echo "foobar" >submodule/sub/a &&
	git -C submodule/sub add a &&
	git -C submodule/sub commit -m "add a" &&
	git -C submodule submodule add ./sub &&
	git -C submodule add sub &&
	git -C submodule commit -m "added sub" &&
	git add submodule &&
	git commit -m "updated submodule" &&

	cat >expect <<-\EOF &&
	a:foobar
	b/b:bar
	submodule/a:foobar
	submodule/sub/a:foobar
	EOF

	git grep -e "bar" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and multiple patterns' '
	cat >expect <<-\EOF &&
	a:foobar
	submodule/a:foobar
	submodule/sub/a:foobar
	EOF

	git grep -e "bar" --and -e "foo" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and multiple patterns' '
	cat >expect <<-\EOF &&
	b/b:bar
	EOF

	git grep -e "bar" --and --not -e "foo" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_incompatible_with_recurse_submodules ()
{
	test_expect_success "--recurse-submodules and $1 are incompatible" "
		test_must_fail git grep -e. --recurse-submodules $1 2>actual &&
		test_i18ngrep 'not supported with --recurse-submodules' actual
	"
}

test_incompatible_with_recurse_submodules --untracked
test_incompatible_with_recurse_submodules --no-index
test_incompatible_with_recurse_submodules HEAD

test_done
