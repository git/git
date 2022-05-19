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
	but add a b &&
	but cummit -m "add a and b" &&
	but init submodule &&
	echo c >submodule/c &&
	but -C submodule add c &&
	but -C submodule cummit -m "add c" &&
	but submodule add ./submodule &&
	but cummit -m "added submodule"
'

test_expect_success 'ls-files correctly outputs files in submodule' '
	cat >expect <<-\EOF &&
	.butmodules
	a
	b/b
	submodule/c
	EOF

	but ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success '--stage' '
	GITMODULES_HASH=$(but rev-parse HEAD:.butmodules) &&
	A_HASH=$(but rev-parse HEAD:a) &&
	B_HASH=$(but rev-parse HEAD:b/b) &&
	C_HASH=$(but -C submodule rev-parse HEAD:c) &&

	cat >expect <<-EOF &&
	100644 $GITMODULES_HASH 0	.butmodules
	100644 $A_HASH 0	a
	100644 $B_HASH 0	b/b
	100644 $C_HASH 0	submodule/c
	EOF

	but ls-files --stage --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-files correctly outputs files in submodule with -z' '
	lf_to_nul >expect <<-\EOF &&
	.butmodules
	a
	b/b
	submodule/c
	EOF

	but ls-files --recurse-submodules -z >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-files does not output files not added to a repo' '
	cat >expect <<-\EOF &&
	.butmodules
	a
	b/b
	submodule/c
	EOF

	echo a >not_added &&
	echo b >b/not_added &&
	echo c >submodule/not_added &&
	but ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-files recurses more than 1 level' '
	cat >expect <<-\EOF &&
	.butmodules
	a
	b/b
	submodule/.butmodules
	submodule/c
	submodule/subsub/d
	EOF

	but init submodule/subsub &&
	echo d >submodule/subsub/d &&
	but -C submodule/subsub add d &&
	but -C submodule/subsub cummit -m "add d" &&
	but -C submodule submodule add ./subsub &&
	but -C submodule cummit -m "added subsub" &&
	but submodule absorbbutdirs &&
	but ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-files works with GIT_DIR' '
	cat >expect <<-\EOF &&
	.butmodules
	c
	subsub/d
	EOF

	but --but-dir=submodule/.but ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs setup' '
	echo e >submodule/subsub/e.txt &&
	but -C submodule/subsub add e.txt &&
	but -C submodule/subsub cummit -m "adding e.txt" &&
	echo f >submodule/f.TXT &&
	echo g >submodule/g.txt &&
	but -C submodule add f.TXT g.txt &&
	but -C submodule cummit -m "add f and g" &&
	echo h >h.txt &&
	mkdir sib &&
	echo sib >sib/file &&
	but add h.txt sib/file &&
	but cummit -m "add h and sib/file" &&
	but init sub &&
	echo sub >sub/file &&
	but -C sub add file &&
	but -C sub cummit -m "add file" &&
	but submodule add ./sub &&
	but cummit -m "added sub" &&

	cat >expect <<-\EOF &&
	.butmodules
	a
	b/b
	h.txt
	sib/file
	sub/file
	submodule/.butmodules
	submodule/c
	submodule/f.TXT
	submodule/g.txt
	submodule/subsub/d
	submodule/subsub/e.txt
	EOF

	but ls-files --recurse-submodules >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "*" >actual &&
	test_cmp expect actual
'

test_expect_success 'inactive submodule' '
	test_when_finished "but config --bool submodule.submodule.active true" &&
	test_when_finished "but -C submodule config --bool submodule.subsub.active true" &&
	but config --bool submodule.submodule.active "false" &&

	cat >expect <<-\EOF &&
	.butmodules
	a
	b/b
	h.txt
	sib/file
	sub/file
	submodule
	EOF

	but ls-files --recurse-submodules >actual &&
	test_cmp expect actual &&

	but config --bool submodule.submodule.active "true" &&
	but -C submodule config --bool submodule.subsub.active "false" &&

	cat >expect <<-\EOF &&
	.butmodules
	a
	b/b
	h.txt
	sib/file
	sub/file
	submodule/.butmodules
	submodule/c
	submodule/f.TXT
	submodule/g.txt
	submodule/subsub
	EOF

	but ls-files --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	h.txt
	submodule/g.txt
	submodule/subsub/e.txt
	EOF

	but ls-files --recurse-submodules "*.txt" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	h.txt
	submodule/f.TXT
	submodule/g.txt
	submodule/subsub/e.txt
	EOF

	but ls-files --recurse-submodules ":(icase)*.txt" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	h.txt
	submodule/f.TXT
	submodule/g.txt
	EOF

	but ls-files --recurse-submodules ":(icase)*.txt" ":(exclude)submodule/subsub/*" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	sub/file
	EOF

	but ls-files --recurse-submodules "sub" >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "sub/" >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "sub/file" >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "su*/file" >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "su?/file" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and pathspecs' '
	cat >expect <<-\EOF &&
	sib/file
	sub/file
	EOF

	but ls-files --recurse-submodules "s??/file" >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "s???file" >actual &&
	test_cmp expect actual &&
	but ls-files --recurse-submodules "s*file" >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules and relative paths' '
	# From subdir
	cat >expect <<-\EOF &&
	b
	EOF
	but -C b ls-files --recurse-submodules >actual &&
	test_cmp expect actual &&

	# Relative path to top
	cat >expect <<-\EOF &&
	../.butmodules
	../a
	b
	../h.txt
	../sib/file
	../sub/file
	../submodule/.butmodules
	../submodule/c
	../submodule/f.TXT
	../submodule/g.txt
	../submodule/subsub/d
	../submodule/subsub/e.txt
	EOF
	but -C b ls-files --recurse-submodules -- .. >actual &&
	test_cmp expect actual &&

	# Relative path to submodule
	cat >expect <<-\EOF &&
	../submodule/.butmodules
	../submodule/c
	../submodule/f.TXT
	../submodule/g.txt
	../submodule/subsub/d
	../submodule/subsub/e.txt
	EOF
	but -C b ls-files --recurse-submodules -- ../submodule >actual &&
	test_cmp expect actual
'

test_expect_success '--recurse-submodules does not support --error-unmatch' '
	test_must_fail but ls-files --recurse-submodules --error-unmatch 2>actual &&
	test_i18ngrep "does not support --error-unmatch" actual
'

test_incompatible_with_recurse_submodules () {
	test_expect_success "--recurse-submodules and $1 are incompatible" "
		test_must_fail but ls-files --recurse-submodules $1 2>actual &&
		test_i18ngrep 'unsupported mode' actual
	"
}

test_incompatible_with_recurse_submodules --deleted
test_incompatible_with_recurse_submodules --modified
test_incompatible_with_recurse_submodules --others
test_incompatible_with_recurse_submodules --killed
test_incompatible_with_recurse_submodules --unmerged

test_done
