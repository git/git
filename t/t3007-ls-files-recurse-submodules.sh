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

test_expect_success 'ls-files correctly outputs files in submodule with -z' '
	lf_to_nul >expect <<-\EOF &&
	.gitmodules
	a
	b/b
	submodule/c
	EOF

	git ls-files --recurse-submodules -z >actual &&
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

test_expect_success '--recurse-submodules and pathspecs setup' '
	echo e >submodule/subsub/e.txt &&
	git -C submodule/subsub add e.txt &&
	git -C submodule/subsub commit -m "adding e.txt" &&
	echo f >submodule/f.TXT &&
	echo g >submodule/g.txt &&
	git -C submodule add f.TXT g.txt &&
	git -C submodule commit -m "add f and g" &&
	echo h >h.txt &&
	mkdir sib &&
	echo sib >sib/file &&
	git add h.txt sib/file &&
	git commit -m "add h and sib/file" &&
	git init sub &&
	echo sub >sub/file &&
	git -C sub add file &&
	git -C sub commit -m "add file" &&
	git submodule add ./sub &&
	git commit -m "added sub" &&

	cat >expect <<-\EOF &&
	.gitmodules
	a
	b/b
	h.txt
	sib/file
	sub/file
	submodule/.gitmodules
	submodule/c
	submodule/f.TXT
	submodule/g.txt
	submodule/subsub/d
	submodule/subsub/e.txt
	EOF

	git ls-files --recurse-submodules >actual &&
	test_cmp expect actual &&
	cat actual &&
	git ls-files --recurse-submodules "*" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	h.txt
	submodule/g.txt
	submodule/subsub/e.txt
	EOF

	git ls-files --recurse-submodules "*.txt" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	h.txt
	submodule/f.TXT
	submodule/g.txt
	submodule/subsub/e.txt
	EOF

	git ls-files --recurse-submodules ":(icase)*.txt" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	h.txt
	submodule/f.TXT
	submodule/g.txt
	EOF

	git ls-files --recurse-submodules ":(icase)*.txt" ":(exclude)submodule/subsub/*" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	sub/file
	EOF

	git ls-files --recurse-submodules "sub" >actual &&
	test_cmp expect actual &&
	git ls-files --recurse-submodules "sub/" >actual &&
	test_cmp expect actual &&
	git ls-files --recurse-submodules "sub/file" >actual &&
	test_cmp expect actual &&
	git ls-files --recurse-submodules "su*/file" >actual &&
	test_cmp expect actual &&
	git ls-files --recurse-submodules "su?/file" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	sib/file
	sub/file
	EOF

	git ls-files --recurse-submodules "s??/file" >actual &&
	test_cmp expect actual &&
	git ls-files --recurse-submodules "s???file" >actual &&
	test_cmp expect actual &&
	git ls-files --recurse-submodules "s*file" >actual &&
	test_cmp expect actual
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

test_incompatible_with_recurse_submodules --deleted
test_incompatible_with_recurse_submodules --modified
test_incompatible_with_recurse_submodules --others
test_incompatible_with_recurse_submodules --stage
test_incompatible_with_recurse_submodules --killed
test_incompatible_with_recurse_submodules --unmerged

test_done
