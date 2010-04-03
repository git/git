#!/bin/sh

test_description='test cherry-pick and revert with conflicts

  -
  + picked: rewrites foo to c
  + base: rewrites foo to b
  + initial: writes foo as a, unrelated as unrelated

'

. ./test-lib.sh

test_expect_success setup '

	echo unrelated >unrelated &&
	git add unrelated &&
	test_commit initial foo a &&
	test_commit base foo b &&
	test_commit picked foo c &&
	git config advice.detachedhead false

'

test_expect_success 'failed cherry-pick does not advance HEAD' '

	git checkout -f initial^0 &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	head=$(git rev-parse HEAD) &&
	test_must_fail git cherry-pick picked &&
	newhead=$(git rev-parse HEAD) &&

	test "$head" = "$newhead"
'

test_expect_success 'failed cherry-pick produces dirty index' '

	git checkout -f initial^0 &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	test_must_fail git cherry-pick picked &&

	test_must_fail git update-index --refresh -q &&
	test_must_fail git diff-index --exit-code HEAD
'

test_expect_success 'failed cherry-pick registers participants in index' '

	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&
	{
		git checkout base -- foo &&
		git ls-files --stage foo &&
		git checkout initial -- foo &&
		git ls-files --stage foo &&
		git checkout picked -- foo &&
		git ls-files --stage foo
	} > stages &&
	sed "
		1 s/ 0	/ 1	/
		2 s/ 0	/ 2	/
		3 s/ 0	/ 3	/
	" < stages > expected &&
	git checkout -f initial^0 &&

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	test_must_fail git cherry-pick picked &&
	git ls-files --stage --unmerged > actual &&

	test_cmp expected actual
'

test_expect_success 'failed cherry-pick describes conflict in work tree' '

	git checkout -f initial^0 &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	=======
	c
	>>>>>>> objid picked
	EOF

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	test_must_fail git cherry-pick picked &&

	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_expect_success 'diff3 -m style' '

	git config merge.conflictstyle diff3 &&
	git checkout -f initial^0 &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	||||||| parent of objid picked
	b
	=======
	c
	>>>>>>> objid picked
	EOF

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	test_must_fail git cherry-pick picked &&

	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_expect_success 'revert also handles conflicts sanely' '

	git config --unset merge.conflictstyle &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	=======
	b
	>>>>>>> parent of objid picked
	EOF
	{
		git checkout picked -- foo &&
		git ls-files --stage foo &&
		git checkout initial -- foo &&
		git ls-files --stage foo &&
		git checkout base -- foo &&
		git ls-files --stage foo
	} > stages &&
	sed "
		1 s/ 0	/ 1	/
		2 s/ 0	/ 2	/
		3 s/ 0	/ 3	/
	" < stages > expected-stages &&
	git checkout -f initial^0 &&

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	head=$(git rev-parse HEAD) &&
	test_must_fail git revert picked &&
	newhead=$(git rev-parse HEAD) &&
	git ls-files --stage --unmerged > actual-stages &&

	test "$head" = "$newhead" &&
	test_must_fail git update-index --refresh -q &&
	test_must_fail git diff-index --exit-code HEAD &&
	test_cmp expected-stages actual-stages &&
	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_expect_success 'revert conflict, diff3 -m style' '
	git config merge.conflictstyle diff3 &&
	git checkout -f initial^0 &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	||||||| objid picked
	c
	=======
	b
	>>>>>>> parent of objid picked
	EOF

	git update-index --refresh &&
	git diff-index --exit-code HEAD &&

	test_must_fail git revert picked &&

	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_done
