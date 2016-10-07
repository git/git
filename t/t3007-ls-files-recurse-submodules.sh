#!/bin/sh

test_description='Test ls-files recurse-submodules feature

This test verifies the recurse-submodules feature correctly lists files from
submodules.
'

. ./test-lib.sh

test_expect_success 'setup directory structure and submodules' '
	echo a >a &&
	mkdir b &&
	echo b >b/b &&
	git add a b &&
	git commit -m "add a and b" &&
	git init submodule &&
	echo c >submodule/c &&
	git -C submodule add c &&
	git -C submodule commit -m "add c" &&
	git submodule add ./submodule &&
	git commit -m "added submodule"
'

test_expect_success 'ls-files correctly outputs files in submodule' '
	cat >expect <<-\EOF &&
	.gitmodules
	a
	b/b
	submodule/c
	EOF

	git ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-files does not output files not added to a repo' '
	cat >expect <<-\EOF &&
	.gitmodules
	a
	b/b
	submodule/c
	EOF

	echo a >not_added &&
	echo b >b/not_added &&
	echo c >submodule/not_added &&
	git ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-files recurses more than 1 level' '
	cat >expect <<-\EOF &&
	.gitmodules
	a
	b/b
	submodule/.gitmodules
	submodule/c
	submodule/subsub/d
	EOF

	git init submodule/subsub &&
	echo d >submodule/subsub/d &&
	git -C submodule/subsub add d &&
	git -C submodule/subsub commit -m "add d" &&
	git -C submodule submodule add ./subsub &&
	git -C submodule commit -m "added subsub" &&
	git ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules does not support using path arguments' '
	test_must_fail git ls-files --recurse-submodules b 2>actual &&
	test_i18ngrep "does not support pathspec" actual
'

test_expect_success '--recurse-submodules does not support --error-unmatch' '
	test_must_fail git ls-files --recurse-submodules --error-unmatch 2>actual &&
	test_i18ngrep "does not support --error-unmatch" actual
'

test_incompatible_with_recurse_submodules () {
	test_expect_success "--recurse-submodules and $1 are incompatible" "
		test_must_fail git ls-files --recurse-submodules $1 2>actual &&
		test_i18ngrep 'unsupported mode' actual
	"
}

test_incompatible_with_recurse_submodules -z
test_incompatible_with_recurse_submodules -v
test_incompatible_with_recurse_submodules -t
test_incompatible_with_recurse_submodules --deleted
test_incompatible_with_recurse_submodules --modified
test_incompatible_with_recurse_submodules --others
test_incompatible_with_recurse_submodules --stage
test_incompatible_with_recurse_submodules --killed
test_incompatible_with_recurse_submodules --unmerged
test_incompatible_with_recurse_submodules --eol

test_done
