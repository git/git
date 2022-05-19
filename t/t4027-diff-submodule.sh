#!/bin/sh

test_description='difference in submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

test_expect_success setup '
	test_tick &&
	test_create_repo sub &&
	(
		cd sub &&
		echo hello >world &&
		but add world &&
		but cummit -m submodule
	) &&

	test_tick &&
	echo frotz >nitfol &&
	but add nitfol sub &&
	but cummit -m superproject &&

	(
		cd sub &&
		echo goodbye >world &&
		but add world &&
		but cummit -m "submodule #2"
	) &&

	but -C sub rev-list HEAD >revs &&
	set x $(cat revs) &&
	echo ":160000 160000 $3 $ZERO_OID M	sub" >expect &&
	subtip=$3 subprev=$2
'

test_expect_success 'but diff --raw HEAD' '
	hexsz=$(test_oid hexsz) &&
	but diff --raw --abbrev=$hexsz HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'but diff-index --raw HEAD' '
	but diff-index --raw HEAD >actual.index &&
	test_cmp expect actual.index
'

test_expect_success 'but diff-files --raw' '
	but diff-files --raw >actual.files &&
	test_cmp expect actual.files
'

expect_from_to () {
	printf "%sSubproject cummit %s\n+Subproject cummit %s\n" \
		"-" "$1" "$2"
}

test_expect_success 'but diff HEAD' '
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body
'

test_expect_success 'but diff HEAD with dirty submodule (work tree)' '
	echo >>sub/world &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'but diff HEAD with dirty submodule (index)' '
	(
		cd sub &&
		but reset --hard &&
		echo >>world &&
		but add world
	) &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'but diff HEAD with dirty submodule (untracked)' '
	(
		cd sub &&
		but reset --hard &&
		but clean -qfdx &&
		>cruft
	) &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body
'

test_expect_success 'but diff HEAD with dirty submodule (untracked) (none ignored)' '
	test_config diff.ignoreSubmodules none &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'but diff HEAD with dirty submodule (work tree, refs match)' '
	but cummit -m "x" sub &&
	echo >>sub/world &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	but diff --ignore-submodules HEAD >actual2 &&
	test_must_be_empty actual2 &&
	but diff --ignore-submodules=untracked HEAD >actual3 &&
	sed -e "1,/^@@/d" actual3 >actual3.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual3.body &&
	but diff --ignore-submodules=dirty HEAD >actual4 &&
	test_must_be_empty actual4
'

test_expect_success 'but diff HEAD with dirty submodule (work tree, refs match) [.butmodules]' '
	but config diff.ignoreSubmodules dirty &&
	but diff HEAD >actual &&
	test_must_be_empty actual &&
	but config --add -f .butmodules submodule.subname.ignore none &&
	but config --add -f .butmodules submodule.subname.path sub &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	but config -f .butmodules submodule.subname.ignore all &&
	but config -f .butmodules submodule.subname.path sub &&
	but diff HEAD >actual2 &&
	test_must_be_empty actual2 &&
	but config -f .butmodules submodule.subname.ignore untracked &&
	but diff HEAD >actual3 &&
	sed -e "1,/^@@/d" actual3 >actual3.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual3.body &&
	but config -f .butmodules submodule.subname.ignore dirty &&
	but diff HEAD >actual4 &&
	test_must_be_empty actual4 &&
	but config submodule.subname.ignore none &&
	but config submodule.subname.path sub &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	but config --remove-section submodule.subname &&
	but config --remove-section -f .butmodules submodule.subname &&
	but config --unset diff.ignoreSubmodules &&
	rm .butmodules
'

test_expect_success 'but diff HEAD with dirty submodule (index, refs match)' '
	(
		cd sub &&
		but reset --hard &&
		echo >>world &&
		but add world
	) &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'but diff HEAD with dirty submodule (untracked, refs match)' '
	(
		cd sub &&
		but reset --hard &&
		but clean -qfdx &&
		>cruft
	) &&
	but diff --ignore-submodules=none HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	but diff --ignore-submodules=all HEAD >actual2 &&
	test_must_be_empty actual2 &&
	but diff HEAD >actual3 &&
	test_must_be_empty actual3 &&
	but diff --ignore-submodules=dirty HEAD >actual4 &&
	test_must_be_empty actual4
'

test_expect_success 'but diff HEAD with dirty submodule (untracked, refs match) [.butmodules]' '
	but config --add -f .butmodules submodule.subname.ignore all &&
	but config --add -f .butmodules submodule.subname.path sub &&
	but diff HEAD >actual2 &&
	test_must_be_empty actual2 &&
	but config -f .butmodules submodule.subname.ignore untracked &&
	but diff HEAD >actual3 &&
	test_must_be_empty actual3 &&
	but config -f .butmodules submodule.subname.ignore dirty &&
	but diff HEAD >actual4 &&
	test_must_be_empty actual4 &&
	but config submodule.subname.ignore none &&
	but config submodule.subname.path sub &&
	but diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	but config --remove-section submodule.subname &&
	but config --remove-section -f .butmodules submodule.subname &&
	rm .butmodules
'

test_expect_success 'but diff between submodule cummits' '
	but diff HEAD^..HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body &&
	but diff --ignore-submodules=dirty HEAD^..HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body &&
	but diff --ignore-submodules HEAD^..HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'but diff between submodule cummits [.butmodules]' '
	but diff HEAD^..HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body &&
	but config --add -f .butmodules submodule.subname.ignore dirty &&
	but config --add -f .butmodules submodule.subname.path sub &&
	but diff HEAD^..HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body &&
	but config -f .butmodules submodule.subname.ignore all &&
	but diff HEAD^..HEAD >actual &&
	test_must_be_empty actual &&
	but config submodule.subname.ignore dirty &&
	but config submodule.subname.path sub &&
	but diff  HEAD^..HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	but config --remove-section submodule.subname &&
	but config --remove-section -f .butmodules submodule.subname &&
	rm .butmodules
'

test_expect_success 'but diff (empty submodule dir)' '
	rm -rf sub/* sub/.but &&
	but diff > actual.empty &&
	test_must_be_empty actual.empty
'

test_expect_success 'conflicted submodule setup' '
	c=$(test_oid ff_1) &&
	(
		echo "000000 $ZERO_OID 0	sub" &&
		echo "160000 1$c 1	sub" &&
		echo "160000 2$c 2	sub" &&
		echo "160000 3$c 3	sub"
	) | but update-index --index-info &&
	echo >expect.nosub "diff --cc sub
index 2ffffff,3ffffff..0000000
--- a/sub
+++ b/sub
@@@ -1,1 -1,1 +1,1 @@@
- Subproject cummit 2$c
 -Subproject cummit 3$c
++Subproject cummit $ZERO_OID" &&

	hh=$(but rev-parse HEAD) &&
	sed -e "s/$ZERO_OID/$hh/" expect.nosub >expect.withsub

'

test_expect_success 'combined (empty submodule)' '
	rm -fr sub && mkdir sub &&
	but diff >actual &&
	test_cmp expect.nosub actual
'

test_expect_success 'combined (with submodule)' '
	rm -fr sub &&
	but clone --no-checkout . sub &&
	but diff >actual &&
	test_cmp expect.withsub actual
'



test_done
