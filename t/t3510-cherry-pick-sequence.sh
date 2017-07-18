#!/bin/sh

test_description='Test cherry-pick continuation features

 +  conflicting: rewrites unrelated to conflicting
  + yetanotherpick: rewrites foo to e
  + anotherpick: rewrites foo to d
  + picked: rewrites foo to c
  + unrelatedpick: rewrites unrelated to reallyunrelated
  + base: rewrites foo to b
  + initial: writes foo as a, unrelated as unrelated

'

. ./test-lib.sh

# Repeat first match 10 times
_r10='\1\1\1\1\1\1\1\1\1\1'

pristine_detach () {
	git cherry-pick --quit &&
	git checkout -f "$1^0" &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x
}

test_expect_success setup '
	git config advice.detachedhead false &&
	echo unrelated >unrelated &&
	git add unrelated &&
	test_commit initial foo a &&
	test_commit base foo b &&
	test_commit unrelatedpick unrelated reallyunrelated &&
	test_commit picked foo c &&
	test_commit anotherpick foo d &&
	test_commit yetanotherpick foo e &&
	pristine_detach initial &&
	test_commit conflicting unrelated
'

test_expect_success 'cherry-pick persists data on failure' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick -s base..anotherpick &&
	test_path_is_dir .git/sequencer &&
	test_path_is_file .git/sequencer/head &&
	test_path_is_file .git/sequencer/todo &&
	test_path_is_file .git/sequencer/opts
'

test_expect_success 'cherry-pick mid-cherry-pick-sequence' '
	pristine_detach initial &&
	test_must_fail git cherry-pick base..anotherpick &&
	test_cmp_rev picked CHERRY_PICK_HEAD &&
	# "oops, I forgot that these patches rely on the change from base"
	git checkout HEAD foo &&
	git cherry-pick base &&
	git cherry-pick picked &&
	git cherry-pick --continue &&
	git diff --exit-code anotherpick
'

test_expect_success 'cherry-pick persists opts correctly' '
	pristine_detach initial &&
	test_expect_code 128 git cherry-pick -s -m 1 --strategy=recursive -X patience -X ours initial..anotherpick &&
	test_path_is_dir .git/sequencer &&
	test_path_is_file .git/sequencer/head &&
	test_path_is_file .git/sequencer/todo &&
	test_path_is_file .git/sequencer/opts &&
	echo "true" >expect &&
	git config --file=.git/sequencer/opts --get-all options.signoff >actual &&
	test_cmp expect actual &&
	echo "1" >expect &&
	git config --file=.git/sequencer/opts --get-all options.mainline >actual &&
	test_cmp expect actual &&
	echo "recursive" >expect &&
	git config --file=.git/sequencer/opts --get-all options.strategy >actual &&
	test_cmp expect actual &&
	cat >expect <<-\EOF &&
	patience
	ours
	EOF
	git config --file=.git/sequencer/opts --get-all options.strategy-option >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick cleans up sequencer state upon success' '
	pristine_detach initial &&
	git cherry-pick initial..picked &&
	test_path_is_missing .git/sequencer
'

test_expect_success '--quit does not complain when no cherry-pick is in progress' '
	pristine_detach initial &&
	git cherry-pick --quit
'

test_expect_success '--abort requires cherry-pick in progress' '
	pristine_detach initial &&
	test_must_fail git cherry-pick --abort
'

test_expect_success '--quit cleans up sequencer state' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..picked &&
	git cherry-pick --quit &&
	test_path_is_missing .git/sequencer
'

test_expect_success '--quit keeps HEAD and conflicted index intact' '
	pristine_detach initial &&
	cat >expect <<-\EOF &&
	OBJID
	:100644 100644 OBJID OBJID M	unrelated
	OBJID
	:000000 100644 OBJID OBJID A	foo
	:000000 100644 OBJID OBJID A	unrelated
	EOF
	test_expect_code 1 git cherry-pick base..picked &&
	git cherry-pick --quit &&
	test_path_is_missing .git/sequencer &&
	test_must_fail git update-index --refresh &&
	{
		git rev-list HEAD |
		git diff-tree --root --stdin |
		sed "s/$_x40/OBJID/g"
	} >actual &&
	test_cmp expect actual
'

test_expect_success '--abort to cancel multiple cherry-pick' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	git cherry-pick --abort &&
	test_path_is_missing .git/sequencer &&
	test_cmp_rev initial HEAD &&
	git update-index --refresh &&
	git diff-index --exit-code HEAD
'

test_expect_success '--abort to cancel single cherry-pick' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick picked &&
	git cherry-pick --abort &&
	test_path_is_missing .git/sequencer &&
	test_cmp_rev initial HEAD &&
	git update-index --refresh &&
	git diff-index --exit-code HEAD
'

test_expect_success 'cherry-pick --abort to cancel multiple revert' '
	pristine_detach anotherpick &&
	test_expect_code 1 git revert base..picked &&
	git cherry-pick --abort &&
	test_path_is_missing .git/sequencer &&
	test_cmp_rev anotherpick HEAD &&
	git update-index --refresh &&
	git diff-index --exit-code HEAD
'

test_expect_success 'revert --abort works, too' '
	pristine_detach anotherpick &&
	test_expect_code 1 git revert base..picked &&
	git revert --abort &&
	test_path_is_missing .git/sequencer &&
	test_cmp_rev anotherpick HEAD
'

test_expect_success '--abort to cancel single revert' '
	pristine_detach anotherpick &&
	test_expect_code 1 git revert picked &&
	git revert --abort &&
	test_path_is_missing .git/sequencer &&
	test_cmp_rev anotherpick HEAD &&
	git update-index --refresh &&
	git diff-index --exit-code HEAD
'

test_expect_success '--abort keeps unrelated change, easy case' '
	pristine_detach unrelatedpick &&
	echo changed >expect &&
	test_expect_code 1 git cherry-pick picked..yetanotherpick &&
	echo changed >unrelated &&
	git cherry-pick --abort &&
	test_cmp expect unrelated
'

test_expect_success '--abort refuses to clobber unrelated change, harder case' '
	pristine_detach initial &&
	echo changed >expect &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo changed >unrelated &&
	test_must_fail git cherry-pick --abort &&
	test_cmp expect unrelated &&
	git rev-list HEAD >log &&
	test_line_count = 2 log &&
	test_must_fail git update-index --refresh &&

	git checkout unrelated &&
	git cherry-pick --abort &&
	test_cmp_rev initial HEAD
'

test_expect_success 'cherry-pick still writes sequencer state when one commit is left' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..picked &&
	test_path_is_dir .git/sequencer &&
	echo "resolved" >foo &&
	git add foo &&
	git commit &&
	{
		git rev-list HEAD |
		git diff-tree --root --stdin |
		sed "s/$_x40/OBJID/g"
	} >actual &&
	cat >expect <<-\EOF &&
	OBJID
	:100644 100644 OBJID OBJID M	foo
	OBJID
	:100644 100644 OBJID OBJID M	unrelated
	OBJID
	:000000 100644 OBJID OBJID A	foo
	:000000 100644 OBJID OBJID A	unrelated
	EOF
	test_cmp expect actual
'

test_expect_success '--abort after last commit in sequence' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..picked &&
	git cherry-pick --abort &&
	test_path_is_missing .git/sequencer &&
	test_cmp_rev initial HEAD &&
	git update-index --refresh &&
	git diff-index --exit-code HEAD
'

test_expect_success 'cherry-pick does not implicitly stomp an existing operation' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	test-chmtime -v +0 .git/sequencer >expect &&
	test_expect_code 128 git cherry-pick unrelatedpick &&
	test-chmtime -v +0 .git/sequencer >actual &&
	test_cmp expect actual
'

test_expect_success '--continue complains when no cherry-pick is in progress' '
	pristine_detach initial &&
	test_expect_code 128 git cherry-pick --continue
'

test_expect_success '--continue complains when there are unresolved conflicts' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	test_expect_code 128 git cherry-pick --continue
'

test_expect_success '--continue of single cherry-pick' '
	pristine_detach initial &&
	echo c >expect &&
	test_must_fail git cherry-pick picked &&
	echo c >foo &&
	git add foo &&
	git cherry-pick --continue &&

	test_cmp expect foo &&
	test_cmp_rev initial HEAD^ &&
	git diff --exit-code HEAD &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success '--continue of single revert' '
	pristine_detach initial &&
	echo resolved >expect &&
	echo "Revert \"picked\"" >expect.msg &&
	test_must_fail git revert picked &&
	echo resolved >foo &&
	git add foo &&
	git cherry-pick --continue &&

	git diff --exit-code HEAD &&
	test_cmp expect foo &&
	test_cmp_rev initial HEAD^ &&
	git diff-tree -s --pretty=tformat:%s HEAD >msg &&
	test_cmp expect.msg msg &&
	test_must_fail git rev-parse --verify CHERRY_PICK_HEAD &&
	test_must_fail git rev-parse --verify REVERT_HEAD
'

test_expect_success '--continue after resolving conflicts' '
	pristine_detach initial &&
	echo d >expect &&
	cat >expect.log <<-\EOF &&
	OBJID
	:100644 100644 OBJID OBJID M	foo
	OBJID
	:100644 100644 OBJID OBJID M	foo
	OBJID
	:100644 100644 OBJID OBJID M	unrelated
	OBJID
	:000000 100644 OBJID OBJID A	foo
	:000000 100644 OBJID OBJID A	unrelated
	EOF
	test_must_fail git cherry-pick base..anotherpick &&
	echo c >foo &&
	git add foo &&
	git cherry-pick --continue &&
	{
		git rev-list HEAD |
		git diff-tree --root --stdin |
		sed "s/$_x40/OBJID/g"
	} >actual.log &&
	test_cmp expect foo &&
	test_cmp expect.log actual.log
'

test_expect_success '--continue after resolving conflicts and committing' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo "c" >foo &&
	git add foo &&
	git commit &&
	git cherry-pick --continue &&
	test_path_is_missing .git/sequencer &&
	{
		git rev-list HEAD |
		git diff-tree --root --stdin |
		sed "s/$_x40/OBJID/g"
	} >actual &&
	cat >expect <<-\EOF &&
	OBJID
	:100644 100644 OBJID OBJID M	foo
	OBJID
	:100644 100644 OBJID OBJID M	foo
	OBJID
	:100644 100644 OBJID OBJID M	unrelated
	OBJID
	:000000 100644 OBJID OBJID A	foo
	:000000 100644 OBJID OBJID A	unrelated
	EOF
	test_cmp expect actual
'

test_expect_success '--continue asks for help after resolving patch to nil' '
	pristine_detach conflicting &&
	test_must_fail git cherry-pick initial..picked &&

	test_cmp_rev unrelatedpick CHERRY_PICK_HEAD &&
	git checkout HEAD -- unrelated &&
	test_must_fail git cherry-pick --continue 2>msg &&
	test_i18ngrep "The previous cherry-pick is now empty" msg
'

test_expect_success 'follow advice and skip nil patch' '
	pristine_detach conflicting &&
	test_must_fail git cherry-pick initial..picked &&

	git checkout HEAD -- unrelated &&
	test_must_fail git cherry-pick --continue &&
	git reset &&
	git cherry-pick --continue &&

	git rev-list initial..HEAD >commits &&
	test_line_count = 3 commits
'

test_expect_success '--continue respects opts' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick -x base..anotherpick &&
	echo "c" >foo &&
	git add foo &&
	git commit &&
	git cherry-pick --continue &&
	test_path_is_missing .git/sequencer &&
	git cat-file commit HEAD >anotherpick_msg &&
	git cat-file commit HEAD~1 >picked_msg &&
	git cat-file commit HEAD~2 >unrelatedpick_msg &&
	git cat-file commit HEAD~3 >initial_msg &&
	test_must_fail grep "cherry picked from" initial_msg &&
	grep "cherry picked from" unrelatedpick_msg &&
	grep "cherry picked from" picked_msg &&
	grep "cherry picked from" anotherpick_msg
'

test_expect_success '--continue of single-pick respects -x' '
	pristine_detach initial &&
	test_must_fail git cherry-pick -x picked &&
	echo c >foo &&
	git add foo &&
	git cherry-pick --continue &&
	test_path_is_missing .git/sequencer &&
	git cat-file commit HEAD >msg &&
	grep "cherry picked from" msg
'

test_expect_success '--continue respects -x in first commit in multi-pick' '
	pristine_detach initial &&
	test_must_fail git cherry-pick -x picked anotherpick &&
	echo c >foo &&
	git add foo &&
	git cherry-pick --continue &&
	test_path_is_missing .git/sequencer &&
	git cat-file commit HEAD^ >msg &&
	picked=$(git rev-parse --verify picked) &&
	grep "cherry picked from.*$picked" msg
'

test_expect_failure '--signoff is automatically propagated to resolved conflict' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick --signoff base..anotherpick &&
	echo "c" >foo &&
	git add foo &&
	git commit &&
	git cherry-pick --continue &&
	test_path_is_missing .git/sequencer &&
	git cat-file commit HEAD >anotherpick_msg &&
	git cat-file commit HEAD~1 >picked_msg &&
	git cat-file commit HEAD~2 >unrelatedpick_msg &&
	git cat-file commit HEAD~3 >initial_msg &&
	test_must_fail grep "Signed-off-by:" initial_msg &&
	grep "Signed-off-by:" unrelatedpick_msg &&
	test_must_fail grep "Signed-off-by:" picked_msg &&
	grep "Signed-off-by:" anotherpick_msg
'

test_expect_failure '--signoff dropped for implicit commit of resolution, multi-pick case' '
	pristine_detach initial &&
	test_must_fail git cherry-pick -s picked anotherpick &&
	echo c >foo &&
	git add foo &&
	git cherry-pick --continue &&

	git diff --exit-code HEAD &&
	test_cmp_rev initial HEAD^^ &&
	git cat-file commit HEAD^ >msg &&
	! grep Signed-off-by: msg
'

test_expect_failure 'sign-off needs to be reaffirmed after conflict resolution, single-pick case' '
	pristine_detach initial &&
	test_must_fail git cherry-pick -s picked &&
	echo c >foo &&
	git add foo &&
	git cherry-pick --continue &&

	git diff --exit-code HEAD &&
	test_cmp_rev initial HEAD^ &&
	git cat-file commit HEAD >msg &&
	! grep Signed-off-by: msg
'

test_expect_success 'malformed instruction sheet 1' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo "resolved" >foo &&
	git add foo &&
	git commit &&
	sed "s/pick /pick/" .git/sequencer/todo >new_sheet &&
	cp new_sheet .git/sequencer/todo &&
	test_expect_code 128 git cherry-pick --continue
'

test_expect_success 'malformed instruction sheet 2' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo "resolved" >foo &&
	git add foo &&
	git commit &&
	sed "s/pick/revert/" .git/sequencer/todo >new_sheet &&
	cp new_sheet .git/sequencer/todo &&
	test_expect_code 128 git cherry-pick --continue
'

test_expect_success 'empty commit set' '
	pristine_detach initial &&
	test_expect_code 128 git cherry-pick base..base
'

test_expect_success 'malformed instruction sheet 3' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo "resolved" >foo &&
	git add foo &&
	git commit &&
	sed "s/pick \([0-9a-f]*\)/pick $_r10/" .git/sequencer/todo >new_sheet &&
	cp new_sheet .git/sequencer/todo &&
	test_expect_code 128 git cherry-pick --continue
'

test_expect_success 'instruction sheet, fat-fingers version' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo "c" >foo &&
	git add foo &&
	git commit &&
	sed "s/pick \([0-9a-f]*\)/pick 	 \1 	/" .git/sequencer/todo >new_sheet &&
	cp new_sheet .git/sequencer/todo &&
	git cherry-pick --continue
'

test_expect_success 'commit descriptions in insn sheet are optional' '
	pristine_detach initial &&
	test_expect_code 1 git cherry-pick base..anotherpick &&
	echo "c" >foo &&
	git add foo &&
	git commit &&
	cut -d" " -f1,2 .git/sequencer/todo >new_sheet &&
	cp new_sheet .git/sequencer/todo &&
	git cherry-pick --continue &&
	test_path_is_missing .git/sequencer &&
	git rev-list HEAD >commits &&
	test_line_count = 4 commits
'

test_done
