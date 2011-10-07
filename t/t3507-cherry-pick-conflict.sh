#!/bin/sh

test_description='test cherry-pick and revert with conflicts

  -
  + picked: rewrites foo to c
  + base: rewrites foo to b
  + initial: writes foo as a, unrelated as unrelated

'

. ./test-lib.sh

test_cmp_rev () {
	git rev-parse --verify "$1" >expect.rev &&
	git rev-parse --verify "$2" >actual.rev &&
	test_cmp expect.rev actual.rev
}

pristine_detach () {
	git checkout -f "$1^0" &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x
}

test_expect_success setup '

	echo unrelated >unrelated &&
	git add unrelated &&
	test_commit initial foo a &&
	test_commit base foo b &&
	test_commit picked foo c &&
	git config advice.detachedhead false

'

test_expect_success 'failed cherry-pick does not advance HEAD' '
	pristine_detach initial &&

	head=$(git rev-parse HEAD) &&
	test_must_fail git cherry-pick picked &&
	newhead=$(git rev-parse HEAD) &&

	test "$head" = "$newhead"
'

test_expect_success 'advice from failed cherry-pick' "
	pristine_detach initial &&

	picked=\$(git rev-parse --short picked) &&
	cat <<-EOF >expected &&
	error: could not apply \$picked... picked
	hint: after resolving the conflicts, mark the corrected paths
	hint: with 'git add <paths>' or 'git rm <paths>'
	hint: and commit the result with 'git commit'
	EOF
	test_must_fail git cherry-pick picked 2>actual &&

	test_i18ncmp expected actual
"

test_expect_success 'failed cherry-pick sets CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	test_must_fail git cherry-pick picked &&
	test_cmp_rev picked CHERRY_PICK_HEAD
'

test_expect_success 'successful cherry-pick does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	git cherry-pick base &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'cherry-pick --no-commit does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	git cherry-pick --no-commit base &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'cherry-pick w/dirty tree does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	echo foo > foo &&
	test_must_fail git cherry-pick base &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success \
	'cherry-pick --strategy=resolve w/dirty tree does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	echo foo > foo &&
	test_must_fail git cherry-pick --strategy=resolve base &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'GIT_CHERRY_PICK_HELP suppresses CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	(
		GIT_CHERRY_PICK_HELP="and then do something else" &&
		export GIT_CHERRY_PICK_HELP &&
		test_must_fail git cherry-pick picked
	) &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'git reset clears CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail git cherry-pick picked &&
	git reset &&

	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'failed commit does not clear CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail git cherry-pick picked &&
	test_must_fail git commit &&

	test_cmp_rev picked CHERRY_PICK_HEAD
'

test_expect_success 'cancelled commit does not clear CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail git cherry-pick picked &&
	echo resolved >foo &&
	git add foo &&
	git update-index --refresh -q &&
	test_must_fail git diff-index --exit-code HEAD &&
	(
		GIT_EDITOR=false &&
		export GIT_EDITOR &&
		test_must_fail git commit
	) &&

	test_cmp_rev picked CHERRY_PICK_HEAD
'

test_expect_success 'successful commit clears CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail git cherry-pick picked &&
	echo resolved >foo &&
	git add foo &&
	git commit &&

	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'failed cherry-pick produces dirty index' '
	pristine_detach initial &&

	test_must_fail git cherry-pick picked &&

	test_must_fail git update-index --refresh -q &&
	test_must_fail git diff-index --exit-code HEAD
'

test_expect_success 'failed cherry-pick registers participants in index' '
	pristine_detach initial &&
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
	git read-tree -u --reset HEAD &&

	test_must_fail git cherry-pick picked &&
	git ls-files --stage --unmerged > actual &&

	test_cmp expected actual
'

test_expect_success 'failed cherry-pick describes conflict in work tree' '
	pristine_detach initial &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	=======
	c
	>>>>>>> objid picked
	EOF

	test_must_fail git cherry-pick picked &&

	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_expect_success 'diff3 -m style' '
	pristine_detach initial &&
	git config merge.conflictstyle diff3 &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	||||||| parent of objid picked
	b
	=======
	c
	>>>>>>> objid picked
	EOF

	test_must_fail git cherry-pick picked &&

	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_expect_success 'revert also handles conflicts sanely' '
	git config --unset merge.conflictstyle &&
	pristine_detach initial &&
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
	git read-tree -u --reset HEAD &&

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
	pristine_detach initial &&
	git config merge.conflictstyle diff3 &&
	cat <<-EOF > expected &&
	<<<<<<< HEAD
	a
	||||||| objid picked
	c
	=======
	b
	>>>>>>> parent of objid picked
	EOF

	test_must_fail git revert picked &&

	sed "s/[a-f0-9]*\.\.\./objid/" foo > actual &&
	test_cmp expected actual
'

test_done
