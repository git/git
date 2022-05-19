#!/bin/sh

test_description='test cherry-pick and revert with conflicts

  -
  + picked: rewrites foo to c
  + base: rewrites foo to b
  + initial: writes foo as a, unrelated as unrelated

'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

pristine_detach () {
	but checkout -f "$1^0" &&
	but read-tree -u --reset HEAD &&
	but clean -d -f -f -q -x
}

test_expect_success setup '

	echo unrelated >unrelated &&
	but add unrelated &&
	test_cummit initial foo a &&
	test_cummit base foo b &&
	test_cummit picked foo c &&
	test_cummit --signoff picked-signed foo d &&
	but checkout -b topic initial &&
	test_cummit redundant-pick foo c redundant &&
	but cummit --allow-empty --allow-empty-message &&
	but tag empty &&
	but checkout main &&
	but config advice.detachedhead false

'

test_expect_success 'failed cherry-pick does not advance HEAD' '
	pristine_detach initial &&

	head=$(but rev-parse HEAD) &&
	test_must_fail but cherry-pick picked &&
	newhead=$(but rev-parse HEAD) &&

	test "$head" = "$newhead"
'

test_expect_success 'advice from failed cherry-pick' '
	pristine_detach initial &&

	picked=$(but rev-parse --short picked) &&
	cat <<-EOF >expected &&
	error: could not apply $picked... picked
	hint: After resolving the conflicts, mark them with
	hint: "but add/rm <pathspec>", then run
	hint: "but cherry-pick --continue".
	hint: You can instead skip this cummit with "but cherry-pick --skip".
	hint: To abort and get back to the state before "but cherry-pick",
	hint: run "but cherry-pick --abort".
	EOF
	test_must_fail but cherry-pick picked 2>actual &&

	test_cmp expected actual
'

test_expect_success 'advice from failed cherry-pick --no-cummit' "
	pristine_detach initial &&

	picked=\$(but rev-parse --short picked) &&
	cat <<-EOF >expected &&
	error: could not apply \$picked... picked
	hint: after resolving the conflicts, mark the corrected paths
	hint: with 'but add <paths>' or 'but rm <paths>'
	EOF
	test_must_fail but cherry-pick --no-cummit picked 2>actual &&

	test_cmp expected actual
"

test_expect_success 'failed cherry-pick sets CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	test_must_fail but cherry-pick picked &&
	test_cmp_rev picked CHERRY_PICK_HEAD
'

test_expect_success 'successful cherry-pick does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	but cherry-pick base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'cherry-pick --no-cummit does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	but cherry-pick --no-cummit base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'cherry-pick w/dirty tree does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	echo foo >foo &&
	test_must_fail but cherry-pick base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success \
	'cherry-pick --strategy=resolve w/dirty tree does not set CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	echo foo >foo &&
	test_must_fail but cherry-pick --strategy=resolve base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'BUT_CHERRY_PICK_HELP suppresses CHERRY_PICK_HEAD' '
	pristine_detach initial &&
	(
		BUT_CHERRY_PICK_HELP="and then do something else" &&
		export BUT_CHERRY_PICK_HELP &&
		test_must_fail but cherry-pick picked
	) &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'but reset clears CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&
	but reset &&

	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'failed cummit does not clear CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&
	test_must_fail but cummit &&

	test_cmp_rev picked CHERRY_PICK_HEAD
'

test_expect_success 'cancelled cummit does not clear CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&
	echo resolved >foo &&
	but add foo &&
	but update-index --refresh -q &&
	test_must_fail but diff-index --exit-code HEAD &&
	(
		BUT_EDITOR=false &&
		export BUT_EDITOR &&
		test_must_fail but cummit
	) &&

	test_cmp_rev picked CHERRY_PICK_HEAD
'

test_expect_success 'successful cummit clears CHERRY_PICK_HEAD' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&
	echo resolved >foo &&
	but add foo &&
	but cummit &&

	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD
'

test_expect_success 'partial cummit of cherry-pick fails' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&
	echo resolved >foo &&
	but add foo &&
	test_must_fail but cummit foo 2>err &&

	test_i18ngrep "cannot do a partial cummit during a cherry-pick." err
'

test_expect_success 'cummit --amend of cherry-pick fails' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&
	echo resolved >foo &&
	but add foo &&
	test_must_fail but cummit --amend 2>err &&

	test_i18ngrep "in the middle of a cherry-pick -- cannot amend." err
'

test_expect_success 'successful final cummit clears cherry-pick state' '
	pristine_detach initial &&

	test_must_fail but cherry-pick base picked-signed &&
	echo resolved >foo &&
	test_path_is_file .but/sequencer/todo &&
	but cummit -a &&
	test_path_is_missing .but/sequencer
'

test_expect_success 'reset after final pick clears cherry-pick state' '
	pristine_detach initial &&

	test_must_fail but cherry-pick base picked-signed &&
	echo resolved >foo &&
	test_path_is_file .but/sequencer/todo &&
	but reset &&
	test_path_is_missing .but/sequencer
'

test_expect_success 'failed cherry-pick produces dirty index' '
	pristine_detach initial &&

	test_must_fail but cherry-pick picked &&

	test_must_fail but update-index --refresh -q &&
	test_must_fail but diff-index --exit-code HEAD
'

test_expect_success 'failed cherry-pick registers participants in index' '
	pristine_detach initial &&
	{
		but checkout base -- foo &&
		but ls-files --stage foo &&
		but checkout initial -- foo &&
		but ls-files --stage foo &&
		but checkout picked -- foo &&
		but ls-files --stage foo
	} >stages &&
	sed "
		1 s/ 0	/ 1	/
		2 s/ 0	/ 2	/
		3 s/ 0	/ 3	/
	" stages >expected &&
	but read-tree -u --reset HEAD &&

	test_must_fail but cherry-pick picked &&
	but ls-files --stage --unmerged >actual &&

	test_cmp expected actual
'

test_expect_success \
	'cherry-pick conflict, ensure cummit.cleanup = scissors places scissors line properly' '
	pristine_detach initial &&
	but config cummit.cleanup scissors &&
	cat <<-EOF >expected &&
		picked

		# ------------------------ >8 ------------------------
		# Do not modify or remove the line above.
		# Everything below it will be ignored.
		#
		# Conflicts:
		#	foo
		EOF

	test_must_fail but cherry-pick picked &&

	test_cmp expected .but/MERGE_MSG
'

test_expect_success \
	'cherry-pick conflict, ensure cleanup=scissors places scissors line properly' '
	pristine_detach initial &&
	but config --unset cummit.cleanup &&
	cat <<-EOF >expected &&
		picked

		# ------------------------ >8 ------------------------
		# Do not modify or remove the line above.
		# Everything below it will be ignored.
		#
		# Conflicts:
		#	foo
		EOF

	test_must_fail but cherry-pick --cleanup=scissors picked &&

	test_cmp expected .but/MERGE_MSG
'

test_expect_success 'failed cherry-pick describes conflict in work tree' '
	pristine_detach initial &&
	cat <<-EOF >expected &&
	<<<<<<< HEAD
	a
	=======
	c
	>>>>>>> objid (picked)
	EOF

	test_must_fail but cherry-pick picked &&

	sed "s/[a-f0-9]* (/objid (/" foo >actual &&
	test_cmp expected actual
'

test_expect_success 'diff3 -m style' '
	pristine_detach initial &&
	but config merge.conflictstyle diff3 &&
	cat <<-EOF >expected &&
	<<<<<<< HEAD
	a
	||||||| parent of objid (picked)
	b
	=======
	c
	>>>>>>> objid (picked)
	EOF

	test_must_fail but cherry-pick picked &&

	sed "s/[a-f0-9]* (/objid (/" foo >actual &&
	test_cmp expected actual
'

test_expect_success 'revert also handles conflicts sanely' '
	but config --unset merge.conflictstyle &&
	pristine_detach initial &&
	cat <<-EOF >expected &&
	<<<<<<< HEAD
	a
	=======
	b
	>>>>>>> parent of objid (picked)
	EOF
	{
		but checkout picked -- foo &&
		but ls-files --stage foo &&
		but checkout initial -- foo &&
		but ls-files --stage foo &&
		but checkout base -- foo &&
		but ls-files --stage foo
	} >stages &&
	sed "
		1 s/ 0	/ 1	/
		2 s/ 0	/ 2	/
		3 s/ 0	/ 3	/
	" stages >expected-stages &&
	but read-tree -u --reset HEAD &&

	head=$(but rev-parse HEAD) &&
	test_must_fail but revert picked &&
	newhead=$(but rev-parse HEAD) &&
	but ls-files --stage --unmerged >actual-stages &&

	test "$head" = "$newhead" &&
	test_must_fail but update-index --refresh -q &&
	test_must_fail but diff-index --exit-code HEAD &&
	test_cmp expected-stages actual-stages &&
	sed "s/[a-f0-9]* (/objid (/" foo >actual &&
	test_cmp expected actual
'

test_expect_success 'failed revert sets REVERT_HEAD' '
	pristine_detach initial &&
	test_must_fail but revert picked &&
	test_cmp_rev picked REVERT_HEAD
'

test_expect_success 'successful revert does not set REVERT_HEAD' '
	pristine_detach base &&
	but revert base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD &&
	test_must_fail but rev-parse --verify REVERT_HEAD
'

test_expect_success 'revert --no-cummit sets REVERT_HEAD' '
	pristine_detach base &&
	but revert --no-cummit base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD &&
	test_cmp_rev base REVERT_HEAD
'

test_expect_success 'revert w/dirty tree does not set REVERT_HEAD' '
	pristine_detach base &&
	echo foo >foo &&
	test_must_fail but revert base &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD &&
	test_must_fail but rev-parse --verify REVERT_HEAD
'

test_expect_success 'BUT_CHERRY_PICK_HELP does not suppress REVERT_HEAD' '
	pristine_detach initial &&
	(
		BUT_CHERRY_PICK_HELP="and then do something else" &&
		BUT_REVERT_HELP="and then do something else, again" &&
		export BUT_CHERRY_PICK_HELP BUT_REVERT_HELP &&
		test_must_fail but revert picked
	) &&
	test_must_fail but rev-parse --verify CHERRY_PICK_HEAD &&
	test_cmp_rev picked REVERT_HEAD
'

test_expect_success 'but reset clears REVERT_HEAD' '
	pristine_detach initial &&
	test_must_fail but revert picked &&
	but reset &&
	test_must_fail but rev-parse --verify REVERT_HEAD
'

test_expect_success 'failed cummit does not clear REVERT_HEAD' '
	pristine_detach initial &&
	test_must_fail but revert picked &&
	test_must_fail but cummit &&
	test_cmp_rev picked REVERT_HEAD
'

test_expect_success 'successful final cummit clears revert state' '
	pristine_detach picked-signed &&

	test_must_fail but revert picked-signed base &&
	echo resolved >foo &&
	test_path_is_file .but/sequencer/todo &&
	but cummit -a &&
	test_path_is_missing .but/sequencer
'

test_expect_success 'reset after final pick clears revert state' '
	pristine_detach picked-signed &&

	test_must_fail but revert picked-signed base &&
	echo resolved >foo &&
	test_path_is_file .but/sequencer/todo &&
	but reset &&
	test_path_is_missing .but/sequencer
'

test_expect_success 'revert conflict, diff3 -m style' '
	pristine_detach initial &&
	but config merge.conflictstyle diff3 &&
	cat <<-EOF >expected &&
	<<<<<<< HEAD
	a
	||||||| objid (picked)
	c
	=======
	b
	>>>>>>> parent of objid (picked)
	EOF

	test_must_fail but revert picked &&

	sed "s/[a-f0-9]* (/objid (/" foo >actual &&
	test_cmp expected actual
'

test_expect_success \
	'revert conflict, ensure cummit.cleanup = scissors places scissors line properly' '
	pristine_detach initial &&
	but config cummit.cleanup scissors &&
	cat >expected <<-EOF &&
		Revert "picked"

		This reverts cummit OBJID.

		# ------------------------ >8 ------------------------
		# Do not modify or remove the line above.
		# Everything below it will be ignored.
		#
		# Conflicts:
		#	foo
		EOF

	test_must_fail but revert picked &&

	sed "s/$OID_REGEX/OBJID/" .but/MERGE_MSG >actual &&
	test_cmp expected actual
'

test_expect_success \
	'revert conflict, ensure cleanup=scissors places scissors line properly' '
	pristine_detach initial &&
	but config --unset cummit.cleanup &&
	cat >expected <<-EOF &&
		Revert "picked"

		This reverts cummit OBJID.

		# ------------------------ >8 ------------------------
		# Do not modify or remove the line above.
		# Everything below it will be ignored.
		#
		# Conflicts:
		#	foo
		EOF

	test_must_fail but revert --cleanup=scissors picked &&

	sed "s/$OID_REGEX/OBJID/" .but/MERGE_MSG >actual &&
	test_cmp expected actual
'

test_expect_success 'failed cherry-pick does not forget -s' '
	pristine_detach initial &&
	test_must_fail but cherry-pick -s picked &&
	test_i18ngrep -e "Signed-off-by" .but/MERGE_MSG
'

test_expect_success 'cummit after failed cherry-pick does not add duplicated -s' '
	pristine_detach initial &&
	test_must_fail but cherry-pick -s picked-signed &&
	but cummit -a -s &&
	test $(but show -s >tmp && grep -c "Signed-off-by" tmp && rm tmp) = 1
'

test_expect_success 'cummit after failed cherry-pick adds -s at the right place' '
	pristine_detach initial &&
	test_must_fail but cherry-pick picked &&

	but cummit -a -s &&

	# Do S-o-b and Conflicts appear in the right order?
	cat <<-\EOF >expect &&
	Signed-off-by: C O Mitter <cummitter@example.com>
	# Conflicts:
	EOF
	grep -e "^# Conflicts:" -e "^Signed-off-by" .but/CUMMIT_EDITMSG >actual &&
	test_cmp expect actual &&

	cat <<-\EOF >expected &&
	picked

	Signed-off-by: C O Mitter <cummitter@example.com>
	EOF

	but show -s --pretty=format:%B >actual &&
	test_cmp expected actual
'

test_expect_success 'cummit --amend -s places the sign-off at the right place' '
	pristine_detach initial &&
	test_must_fail but cherry-pick picked &&

	# emulate old-style conflicts block
	mv .but/MERGE_MSG .but/MERGE_MSG+ &&
	sed -e "/^# Conflicts:/,\$s/^# *//" .but/MERGE_MSG+ >.but/MERGE_MSG &&

	but cummit -a &&
	but cummit --amend -s &&

	# Do S-o-b and Conflicts appear in the right order?
	cat <<-\EOF >expect &&
	Signed-off-by: C O Mitter <cummitter@example.com>
	Conflicts:
	EOF
	grep -e "^Conflicts:" -e "^Signed-off-by" .but/CUMMIT_EDITMSG >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick preserves sparse-checkout' '
	pristine_detach initial &&
	test_config core.sparseCheckout true &&
	test_when_finished "
		echo \"/*\" >.but/info/sparse-checkout
		but read-tree --reset -u HEAD
		rm .but/info/sparse-checkout" &&
	echo /unrelated >.but/info/sparse-checkout &&
	but read-tree --reset -u HEAD &&
	test_must_fail but cherry-pick -Xours picked>actual &&
	test_i18ngrep ! "Changes not staged for cummit:" actual
'

test_expect_success 'cherry-pick --continue remembers --keep-redundant-cummits' '
	test_when_finished "but cherry-pick --abort || :" &&
	pristine_detach initial &&
	test_must_fail but cherry-pick --keep-redundant-cummits picked redundant &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue
'

test_expect_success 'cherry-pick --continue remembers --allow-empty and --allow-empty-message' '
	test_when_finished "but cherry-pick --abort || :" &&
	pristine_detach initial &&
	test_must_fail but cherry-pick --allow-empty --allow-empty-message \
				       picked empty &&
	echo c >foo &&
	but add foo &&
	but cherry-pick --continue
'

test_done
