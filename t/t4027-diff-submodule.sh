#!/bin/sh

test_description='difference in submodules'

. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

_z40=0000000000000000000000000000000000000000
test_expect_success setup '
	test_tick &&
	test_create_repo sub &&
	(
		cd sub &&
		echo hello >world &&
		git add world &&
		git commit -m submodule
	) &&

	test_tick &&
	echo frotz >nitfol &&
	git add nitfol sub &&
	git commit -m superproject &&

	(
		cd sub &&
		echo goodbye >world &&
		git add world &&
		git commit -m "submodule #2"
	) &&

	set x $(
		cd sub &&
		git rev-list HEAD
	) &&
	echo ":160000 160000 $3 $_z40 M	sub" >expect &&
	subtip=$3 subprev=$2
'

test_expect_success 'git diff --raw HEAD' '
	git diff --raw --abbrev=40 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'git diff-index --raw HEAD' '
	git diff-index --raw HEAD >actual.index &&
	test_cmp expect actual.index
'

test_expect_success 'git diff-files --raw' '
	git diff-files --raw >actual.files &&
	test_cmp expect actual.files
'

expect_from_to () {
	printf "%sSubproject commit %s\n+Subproject commit %s\n" \
		"-" "$1" "$2"
}

test_expect_success 'git diff HEAD' '
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev &&
	test_cmp expect.body actual.body
'

test_expect_success 'git diff HEAD with dirty submodule (work tree)' '
	echo >>sub/world &&
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'git diff HEAD with dirty submodule (index)' '
	(
		cd sub &&
		git reset --hard &&
		echo >>world &&
		git add world
	) &&
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'git diff HEAD with dirty submodule (untracked)' '
	(
		cd sub &&
		git reset --hard &&
		git clean -qfdx &&
		>cruft
	) &&
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subtip $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'git diff HEAD with dirty submodule (work tree, refs match)' '
	git commit -m "x" sub &&
	echo >>sub/world &&
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	git diff --ignore-submodules HEAD >actual2 &&
	! test -s actual2 &&
	git diff --ignore-submodules=untracked HEAD >actual3 &&
	sed -e "1,/^@@/d" actual3 >actual3.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual3.body &&
	git diff --ignore-submodules=dirty HEAD >actual4 &&
	! test -s actual4
'

test_expect_success 'git diff HEAD with dirty submodule (index, refs match)' '
	(
		cd sub &&
		git reset --hard &&
		echo >>world &&
		git add world
	) &&
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body
'

test_expect_success 'git diff HEAD with dirty submodule (untracked, refs match)' '
	(
		cd sub &&
		git reset --hard &&
		git clean -qfdx &&
		>cruft
	) &&
	git diff HEAD >actual &&
	sed -e "1,/^@@/d" actual >actual.body &&
	expect_from_to >expect.body $subprev $subprev-dirty &&
	test_cmp expect.body actual.body &&
	git diff --ignore-submodules=all HEAD >actual2 &&
	! test -s actual2 &&
	git diff --ignore-submodules=untracked HEAD >actual3 &&
	! test -s actual3 &&
	git diff --ignore-submodules=dirty HEAD >actual4 &&
	! test -s actual4
'

test_expect_success 'git diff (empty submodule dir)' '
	: >empty &&
	rm -rf sub/* sub/.git &&
	git diff > actual.empty &&
	test_cmp empty actual.empty
'

test_expect_success 'conflicted submodule setup' '

	# 39 efs
	c=fffffffffffffffffffffffffffffffffffffff
	(
		echo "000000 $_z40 0	sub"
		echo "160000 1$c 1	sub"
		echo "160000 2$c 2	sub"
		echo "160000 3$c 3	sub"
	) | git update-index --index-info &&
	echo >expect.nosub '\''diff --cc sub
index 2ffffff,3ffffff..0000000
--- a/sub
+++ b/sub
@@@ -1,1 -1,1 +1,1 @@@
- Subproject commit 2fffffffffffffffffffffffffffffffffffffff
 -Subproject commit 3fffffffffffffffffffffffffffffffffffffff
++Subproject commit 0000000000000000000000000000000000000000'\'' &&

	hh=$(git rev-parse HEAD) &&
	sed -e "s/$_z40/$hh/" expect.nosub >expect.withsub

'

test_expect_success 'combined (empty submodule)' '
	rm -fr sub && mkdir sub &&
	git diff >actual &&
	test_cmp expect.nosub actual
'

test_expect_success 'combined (with submodule)' '
	rm -fr sub &&
	git clone --no-checkout . sub &&
	git diff >actual &&
	test_cmp expect.withsub actual
'



test_done
