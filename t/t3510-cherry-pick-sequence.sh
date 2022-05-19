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
	but cherry-pick --quit &&
	but checkout -f "$1^0" &&
	but read-tree -u --reset HEAD &&
	but clean -d -f -f -q -x
}

test_expect_success setup '
	but config advice.detachedhead false &&
	echo unrelated >unrelated &&
	but add unrelated &&
	test_cummit initial foo a &&
	test_cummit base foo b &&
	test_cummit unrelatedpick unrelated reallyunrelated &&
	test_cummit picked foo c &&
	test_cummit anotherpick foo d &&
	test_cummit yetanotherpick foo e &&
	pristine_detach initial &&
	test_cummit conflicting unrelated
'

test_expect_success 'cherry-pick persists data on failure' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick -s base..anotherpick &&
	test_path_is_dir .but/sequencer &&
	test_path_is_file .but/sequencer/head &&
	test_path_is_file .but/sequencer/todo &&
	test_path_is_file .but/sequencer/opts
'

test_expect_success 'cherry-pick mid-cherry-pick-sequence' '
	pristine_detach initial &&
	test_must_fail but cherry-pick base..anotherpick &&
	test_cmp_rev picked CHERRY_PICK_HEAD &&
	# "oops, I forgot that these patches rely on the change from base"
	but checkout HEAD foo &&
	but cherry-pick base &&
	but cherry-pick picked &&
	but cherry-pick --continue &&
	but diff --exit-code anotherpick
'

test_expect_success 'cherry-pick persists opts correctly' '
	pristine_detach initial &&
	# to make sure that the session to cherry-pick a sequence
	# gets interrupted, use a high-enough number that is larger
	# than the number of parents of any cummit we have created
	mainline=4 &&
	test_expect_code 128 but cherry-pick -s -m $mainline --strategy=recursive -X patience -X ours --edit initial..anotherpick &&
	test_path_is_dir .but/sequencer &&
	test_path_is_file .but/sequencer/head &&
	test_path_is_file .but/sequencer/todo &&
	test_path_is_file .but/sequencer/opts &&
	echo "true" >expect &&
	but config --file=.but/sequencer/opts --get-all options.signoff >actual &&
	test_cmp expect actual &&
	echo "$mainline" >expect &&
	but config --file=.but/sequencer/opts --get-all options.mainline >actual &&
	test_cmp expect actual &&
	echo "recursive" >expect &&
	but config --file=.but/sequencer/opts --get-all options.strategy >actual &&
	test_cmp expect actual &&
	cat >expect <<-\EOF &&
	patience
	ours
	EOF
	but config --file=.but/sequencer/opts --get-all options.strategy-option >actual &&
	test_cmp expect actual &&
	echo "true" >expect &&
	but config --file=.but/sequencer/opts --get-all options.edit >actual &&
	test_cmp expect actual
'

test_expect_success 'revert persists opts correctly' '
	pristine_detach initial &&
	# to make sure that the session to revert a sequence
	# gets interrupted, revert cummits that are not in the history
	# of HEAD.
	test_expect_code 1 but revert -s --strategy=recursive -X patience -X ours --no-edit picked yetanotherpick &&
	test_path_is_dir .but/sequencer &&
	test_path_is_file .but/sequencer/head &&
	test_path_is_file .but/sequencer/todo &&
	test_path_is_file .but/sequencer/opts &&
	echo "true" >expect &&
	but config --file=.but/sequencer/opts --get-all options.signoff >actual &&
	test_cmp expect actual &&
	echo "recursive" >expect &&
	but config --file=.but/sequencer/opts --get-all options.strategy >actual &&
	test_cmp expect actual &&
	cat >expect <<-\EOF &&
	patience
	ours
	EOF
	but config --file=.but/sequencer/opts --get-all options.strategy-option >actual &&
	test_cmp expect actual &&
	echo "false" >expect &&
	but config --file=.but/sequencer/opts --get-all options.edit >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick cleans up sequencer state upon success' '
	pristine_detach initial &&
	but cherry-pick initial..picked &&
	test_path_is_missing .but/sequencer
'

test_expect_success 'cherry-pick --skip requires cherry-pick in progress' '
	pristine_detach initial &&
	test_must_fail but cherry-pick --skip
'

test_expect_success 'revert --skip requires revert in progress' '
	pristine_detach initial &&
	test_must_fail but revert --skip
'

test_expect_success 'cherry-pick --skip to skip cummit' '
	pristine_detach initial &&
	test_must_fail but cherry-pick anotherpick &&
	test_must_fail but revert --skip &&
	but cherry-pick --skip &&
	test_cmp_rev initial HEAD &&
	test_path_is_missing .but/CHERRY_PICK_HEAD
'

test_expect_success 'revert --skip to skip cummit' '
	pristine_detach anotherpick &&
	test_must_fail but revert anotherpick~1 &&
	test_must_fail but cherry-pick --skip &&
	but revert --skip &&
	test_cmp_rev anotherpick HEAD
'

test_expect_success 'skip "empty" cummit' '
	pristine_detach picked &&
	test_cummit dummy foo d &&
	test_must_fail but cherry-pick anotherpick 2>err &&
	test_i18ngrep "but cherry-pick --skip" err &&
	but cherry-pick --skip &&
	test_cmp_rev dummy HEAD
'

test_expect_success 'skip a cummit and check if rest of sequence is correct' '
	pristine_detach initial &&
	echo e >expect &&
	cat >expect.log <<-EOF &&
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
	test_must_fail but cherry-pick base..yetanotherpick &&
	test_must_fail but cherry-pick --skip &&
	echo d >foo &&
	but add foo &&
	but cherry-pick --continue &&
	{
		but rev-list HEAD |
		but diff-tree --root --stdin |
		sed "s/$OID_REGEX/OBJID/g"
	} >actual.log &&
	test_cmp expect foo &&
	test_cmp expect.log actual.log
'

test_expect_success 'check advice when we move HEAD by cummitting' '
	pristine_detach initial &&
	cat >expect <<-EOF &&
	error: there is nothing to skip
	hint: have you cummitted already?
	hint: try "but cherry-pick --continue"
	fatal: cherry-pick failed
	EOF
	test_must_fail but cherry-pick base..yetanotherpick &&
	echo c >foo &&
	but cummit -a &&
	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	test_must_fail but cherry-pick --skip 2>advice &&
	test_cmp expect advice
'

test_expect_success 'selectively advise --skip while launching another sequence' '
	pristine_detach initial &&
	cat >expect <<-EOF &&
	error: cherry-pick is already in progress
	hint: try "but cherry-pick (--continue | --skip | --abort | --quit)"
	fatal: cherry-pick failed
	EOF
	test_must_fail but cherry-pick picked..yetanotherpick &&
	test_must_fail but cherry-pick picked..yetanotherpick 2>advice &&
	test_cmp expect advice &&
	cat >expect <<-EOF &&
	error: cherry-pick is already in progress
	hint: try "but cherry-pick (--continue | --abort | --quit)"
	fatal: cherry-pick failed
	EOF
	but reset --merge &&
	test_must_fail but cherry-pick picked..yetanotherpick 2>advice &&
	test_cmp expect advice
'

test_expect_success 'allow skipping cummit but not abort for a new history' '
	pristine_detach initial &&
	cat >expect <<-EOF &&
	error: cannot abort from a branch yet to be born
	fatal: cherry-pick failed
	EOF
	but checkout --orphan new_disconnected &&
	but reset --hard &&
	test_must_fail but cherry-pick anotherpick &&
	test_must_fail but cherry-pick --abort 2>advice &&
	but cherry-pick --skip &&
	test_cmp expect advice
'

test_expect_success 'allow skipping stopped cherry-pick because of untracked file modifications' '
	test_when_finished "rm unrelated" &&
	pristine_detach initial &&
	but rm --cached unrelated &&
	but cummit -m "untrack unrelated" &&
	test_must_fail but cherry-pick initial base &&
	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	but cherry-pick --skip
'

test_expect_success '--quit does not complain when no cherry-pick is in progress' '
	pristine_detach initial &&
	but cherry-pick --quit
'

test_expect_success '--abort requires cherry-pick in progress' '
	pristine_detach initial &&
	test_must_fail but cherry-pick --abort
'

test_expect_success '--quit cleans up sequencer state' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..picked &&
	but cherry-pick --quit &&
	test_path_is_missing .but/sequencer &&
	test_path_is_missing .but/CHERRY_PICK_HEAD
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
	test_expect_code 1 but cherry-pick base..picked &&
	but cherry-pick --quit &&
	test_path_is_missing .but/sequencer &&
	test_must_fail but update-index --refresh &&
	{
		but rev-list HEAD |
		but diff-tree --root --stdin |
		sed "s/$OID_REGEX/OBJID/g"
	} >actual &&
	test_cmp expect actual
'

test_expect_success '--abort to cancel multiple cherry-pick' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	but cherry-pick --abort &&
	test_path_is_missing .but/sequencer &&
	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	test_cmp_rev initial HEAD &&
	but update-index --refresh &&
	but diff-index --exit-code HEAD
'

test_expect_success '--abort to cancel single cherry-pick' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick picked &&
	but cherry-pick --abort &&
	test_path_is_missing .but/sequencer &&
	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	test_cmp_rev initial HEAD &&
	but update-index --refresh &&
	but diff-index --exit-code HEAD
'

test_expect_success '--abort does not unsafely change HEAD' '
	pristine_detach initial &&
	test_must_fail but cherry-pick picked anotherpick &&
	but reset --hard base &&
	test_must_fail but cherry-pick picked anotherpick &&
	but cherry-pick --abort 2>actual &&
	test_i18ngrep "You seem to have moved HEAD" actual &&
	test_cmp_rev base HEAD
'

test_expect_success 'cherry-pick --abort to cancel multiple revert' '
	pristine_detach anotherpick &&
	test_expect_code 1 but revert base..picked &&
	but cherry-pick --abort &&
	test_path_is_missing .but/sequencer &&
	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	test_cmp_rev anotherpick HEAD &&
	but update-index --refresh &&
	but diff-index --exit-code HEAD
'

test_expect_success 'revert --abort works, too' '
	pristine_detach anotherpick &&
	test_expect_code 1 but revert base..picked &&
	but revert --abort &&
	test_path_is_missing .but/sequencer &&
	test_cmp_rev anotherpick HEAD
'

test_expect_success '--abort to cancel single revert' '
	pristine_detach anotherpick &&
	test_expect_code 1 but revert picked &&
	but revert --abort &&
	test_path_is_missing .but/sequencer &&
	test_cmp_rev anotherpick HEAD &&
	but update-index --refresh &&
	but diff-index --exit-code HEAD
'

test_expect_success '--abort keeps unrelated change, easy case' '
	pristine_detach unrelatedpick &&
	echo changed >expect &&
	test_expect_code 1 but cherry-pick picked..yetanotherpick &&
	echo changed >unrelated &&
	but cherry-pick --abort &&
	test_cmp expect unrelated
'

test_expect_success '--abort refuses to clobber unrelated change, harder case' '
	pristine_detach initial &&
	echo changed >expect &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo changed >unrelated &&
	test_must_fail but cherry-pick --abort &&
	test_cmp expect unrelated &&
	but rev-list HEAD >log &&
	test_line_count = 2 log &&
	test_must_fail but update-index --refresh &&

	but checkout unrelated &&
	but cherry-pick --abort &&
	test_cmp_rev initial HEAD
'

test_expect_success 'cherry-pick still writes sequencer state when one cummit is left' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..picked &&
	test_path_is_dir .but/sequencer &&
	echo "resolved" >foo &&
	but add foo &&
	but cummit &&
	{
		but rev-list HEAD |
		but diff-tree --root --stdin |
		sed "s/$OID_REGEX/OBJID/g"
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

test_expect_success '--abort after last cummit in sequence' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..picked &&
	but cherry-pick --abort &&
	test_path_is_missing .but/sequencer &&
	test_path_is_missing .but/CHERRY_PICK_HEAD &&
	test_cmp_rev initial HEAD &&
	but update-index --refresh &&
	but diff-index --exit-code HEAD
'

test_expect_success 'cherry-pick does not implicitly stomp an existing operation' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	test-tool chmtime --get .but/sequencer >expect &&
	test_expect_code 128 but cherry-pick unrelatedpick &&
	test-tool chmtime --get .but/sequencer >actual &&
	test_cmp expect actual
'

test_expect_success '--continue complains when no cherry-pick is in progress' '
	pristine_detach initial &&
	test_expect_code 128 but cherry-pick --continue
'

test_expect_success '--continue complains when there are unresolved conflicts' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	test_expect_code 128 but cherry-pick --continue
'

test_expect_success '--continue of single cherry-pick' '
	pristine_detach initial &&
	echo c >expect &&
	test_must_fail but cherry-pick picked &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue &&

	test_cmp expect foo &&
	test_cmp_rev initial HEAD^ &&
	but diff --exit-code HEAD &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success '--continue of single revert' '
	pristine_detach initial &&
	echo resolved >expect &&
	echo "Revert \"picked\"" >expect.msg &&
	test_must_fail but revert picked &&
	echo resolved >foo &&
	but add foo &&
	but cherry-pick --continue &&

	but diff --exit-code HEAD &&
	test_cmp expect foo &&
	test_cmp_rev initial HEAD^ &&
	but diff-tree -s --pretty=tformat:%s HEAD >msg &&
	test_cmp expect.msg msg &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD &&
	test_must_fail but rev-parse --verify REVERT_HEAD
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
	test_must_fail but cherry-pick base..anotherpick &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue &&
	{
		but rev-list HEAD |
		but diff-tree --root --stdin |
		sed "s/$OID_REGEX/OBJID/g"
	} >actual.log &&
	test_cmp expect foo &&
	test_cmp expect.log actual.log
'

test_expect_success '--continue after resolving conflicts and cummitting' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo "c" >foo &&
	but add foo &&
	but cummit &&
	but cherry-pick --continue &&
	test_path_is_missing .but/sequencer &&
	{
		but rev-list HEAD |
		but diff-tree --root --stdin |
		sed "s/$OID_REGEX/OBJID/g"
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
	test_must_fail but cherry-pick initial..picked &&

	test_cmp_rev unrelatedpick CHERRY_PICK_HEAD &&
	but checkout HEAD -- unrelated &&
	test_must_fail but cherry-pick --continue 2>msg &&
	test_i18ngrep "The previous cherry-pick is now empty" msg
'

test_expect_success 'follow advice and skip nil patch' '
	pristine_detach conflicting &&
	test_must_fail but cherry-pick initial..picked &&

	but checkout HEAD -- unrelated &&
	test_must_fail but cherry-pick --continue &&
	but reset &&
	but cherry-pick --continue &&

	but rev-list initial..HEAD >cummits &&
	test_line_count = 3 cummits
'

test_expect_success '--continue respects opts' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick -x base..anotherpick &&
	echo "c" >foo &&
	but add foo &&
	but cummit &&
	but cherry-pick --continue &&
	test_path_is_missing .but/sequencer &&
	but cat-file commit HEAD >anotherpick_msg &&
	but cat-file commit HEAD~1 >picked_msg &&
	but cat-file commit HEAD~2 >unrelatedpick_msg &&
	but cat-file commit HEAD~3 >initial_msg &&
	! grep "cherry picked from" initial_msg &&
	grep "cherry picked from" unrelatedpick_msg &&
	grep "cherry picked from" picked_msg &&
	grep "cherry picked from" anotherpick_msg
'

test_expect_success '--continue of single-pick respects -x' '
	pristine_detach initial &&
	test_must_fail but cherry-pick -x picked &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue &&
	test_path_is_missing .but/sequencer &&
	but cat-file commit HEAD >msg &&
	grep "cherry picked from" msg
'

test_expect_success '--continue respects -x in first cummit in multi-pick' '
	pristine_detach initial &&
	test_must_fail but cherry-pick -x picked anotherpick &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue &&
	test_path_is_missing .but/sequencer &&
	but cat-file commit HEAD^ >msg &&
	picked=$(but rev-parse --verify picked) &&
	grep "cherry picked from.*$picked" msg
'

test_expect_failure '--signoff is automatically propagated to resolved conflict' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick --signoff base..anotherpick &&
	echo "c" >foo &&
	but add foo &&
	but cummit &&
	but cherry-pick --continue &&
	test_path_is_missing .but/sequencer &&
	but cat-file commit HEAD >anotherpick_msg &&
	but cat-file commit HEAD~1 >picked_msg &&
	but cat-file commit HEAD~2 >unrelatedpick_msg &&
	but cat-file commit HEAD~3 >initial_msg &&
	! grep "Signed-off-by:" initial_msg &&
	grep "Signed-off-by:" unrelatedpick_msg &&
	! grep "Signed-off-by:" picked_msg &&
	grep "Signed-off-by:" anotherpick_msg
'

test_expect_failure '--signoff dropped for implicit cummit of resolution, multi-pick case' '
	pristine_detach initial &&
	test_must_fail but cherry-pick -s picked anotherpick &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue &&

	but diff --exit-code HEAD &&
	test_cmp_rev initial HEAD^^ &&
	but cat-file commit HEAD^ >msg &&
	! grep Signed-off-by: msg
'

test_expect_failure 'sign-off needs to be reaffirmed after conflict resolution, single-pick case' '
	pristine_detach initial &&
	test_must_fail but cherry-pick -s picked &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue &&

	but diff --exit-code HEAD &&
	test_cmp_rev initial HEAD^ &&
	but cat-file commit HEAD >msg &&
	! grep Signed-off-by: msg
'

test_expect_success 'malformed instruction sheet 1' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo "resolved" >foo &&
	but add foo &&
	but cummit &&
	sed "s/pick /pick/" .but/sequencer/todo >new_sheet &&
	cp new_sheet .but/sequencer/todo &&
	test_expect_code 128 but cherry-pick --continue
'

test_expect_success 'malformed instruction sheet 2' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo "resolved" >foo &&
	but add foo &&
	but cummit &&
	sed "s/pick/revert/" .but/sequencer/todo >new_sheet &&
	cp new_sheet .but/sequencer/todo &&
	test_expect_code 128 but cherry-pick --continue
'

test_expect_success 'empty cummit set (no cummits to walk)' '
	pristine_detach initial &&
	test_expect_code 128 but cherry-pick base..base
'

test_expect_success 'empty cummit set (culled during walk)' '
	pristine_detach initial &&
	test_expect_code 128 but cherry-pick -2 --author=no.such.author base
'

test_expect_success 'malformed instruction sheet 3' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo "resolved" >foo &&
	but add foo &&
	but cummit &&
	sed "s/pick \([0-9a-f]*\)/pick $_r10/" .but/sequencer/todo >new_sheet &&
	cp new_sheet .but/sequencer/todo &&
	test_expect_code 128 but cherry-pick --continue
'

test_expect_success 'instruction sheet, fat-fingers version' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo "c" >foo &&
	but add foo &&
	but cummit &&
	sed "s/pick \([0-9a-f]*\)/pick 	 \1 	/" .but/sequencer/todo >new_sheet &&
	cp new_sheet .but/sequencer/todo &&
	but cherry-pick --continue
'

test_expect_success 'cummit descriptions in insn sheet are optional' '
	pristine_detach initial &&
	test_expect_code 1 but cherry-pick base..anotherpick &&
	echo "c" >foo &&
	but add foo &&
	but cummit &&
	cut -d" " -f1,2 .but/sequencer/todo >new_sheet &&
	cp new_sheet .but/sequencer/todo &&
	but cherry-pick --continue &&
	test_path_is_missing .but/sequencer &&
	but rev-list HEAD >cummits &&
	test_line_count = 4 cummits
'

test_done
