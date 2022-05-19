#!/bin/sh

test_description='test cherry-pick and revert with renames

  --
   + rename2: renames oops to opos
  +  rename1: renames oops to spoo
  +  added:   adds extra line to oops
  ++ initial: has lines in oops

'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	for l in a b c d e f g h i j k l m n o
	do
		echo $l$l$l$l$l$l$l$l$l || return 1
	done >oops &&

	test_tick &&
	but add oops &&
	but cummit -m initial &&
	but tag initial &&

	test_tick &&
	echo "Add extra line at the end" >>oops &&
	but cummit -a -m added &&
	but tag added &&

	test_tick &&
	but mv oops spoo &&
	but cummit -m rename1 &&
	but tag rename1 &&

	test_tick &&
	but checkout -b side initial &&
	but mv oops opos &&
	but cummit -m rename2 &&
	but tag rename2
'

test_expect_success 'cherry-pick --nonsense' '

	pos=$(but rev-parse HEAD) &&
	but diff --exit-code HEAD &&
	test_must_fail but cherry-pick --nonsense 2>msg &&
	but diff --exit-code HEAD "$pos" &&
	test_i18ngrep "[Uu]sage:" msg
'

test_expect_success 'revert --nonsense' '

	pos=$(but rev-parse HEAD) &&
	but diff --exit-code HEAD &&
	test_must_fail but revert --nonsense 2>msg &&
	but diff --exit-code HEAD "$pos" &&
	test_i18ngrep "[Uu]sage:" msg
'

test_expect_success 'cherry-pick after renaming branch' '

	but checkout rename2 &&
	but cherry-pick added &&
	test $(but rev-parse HEAD^) = $(but rev-parse rename2) &&
	test -f opos &&
	grep "Add extra line at the end" opos &&
	but reflog -1 | grep cherry-pick

'

test_expect_success 'revert after renaming branch' '

	but checkout rename1 &&
	but revert added &&
	test $(but rev-parse HEAD^) = $(but rev-parse rename1) &&
	test -f spoo &&
	! grep "Add extra line at the end" spoo &&
	but reflog -1 | grep revert

'

test_expect_success 'cherry-pick on stat-dirty working tree' '
	but clone . copy &&
	(
		cd copy &&
		but checkout initial &&
		test-tool chmtime +40 oops &&
		but cherry-pick added
	)
'

test_expect_success 'revert forbidden on dirty working tree' '

	echo content >extra_file &&
	but add extra_file &&
	test_must_fail but revert HEAD 2>errors &&
	test_i18ngrep "your local changes would be overwritten by " errors

'

test_expect_success 'cherry-pick on unborn branch' '
	but checkout --orphan unborn &&
	but rm --cached -r . &&
	rm -rf * &&
	but cherry-pick initial &&
	but diff --quiet initial &&
	test_cmp_rev ! initial HEAD
'

test_expect_success 'cherry-pick "-" to pick from previous branch' '
	but checkout unborn &&
	test_cummit to-pick actual content &&
	but checkout main &&
	but cherry-pick - &&
	echo content >expect &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick "-" is meaningless without checkout' '
	test_create_repo afresh &&
	(
		cd afresh &&
		test_cummit one &&
		test_cummit two &&
		test_cummit three &&
		test_must_fail but cherry-pick -
	)
'

test_expect_success 'cherry-pick "-" works with arguments' '
	but checkout -b side-branch &&
	test_cummit change actual change &&
	but checkout main &&
	but cherry-pick -s - &&
	echo "Signed-off-by: C O Mitter <cummitter@example.com>" >expect &&
	but cat-file commit HEAD | grep ^Signed-off-by: >signoff &&
	test_cmp expect signoff &&
	echo change >expect &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick works with dirty renamed file' '
	test_cummit to-rename &&
	but checkout -b unrelated &&
	test_cummit unrelated &&
	but checkout @{-1} &&
	but mv to-rename.t renamed &&
	test_tick &&
	but cummit -m renamed &&
	echo modified >renamed &&
	but cherry-pick refs/heads/unrelated &&
	test $(but rev-parse :0:renamed) = $(but rev-parse HEAD~2:to-rename.t) &&
	grep -q "^modified$" renamed
'

test_expect_success 'advice from failed revert' '
	test_cummit --no-tag "add dream" dream dream &&
	dream_oid=$(but rev-parse --short HEAD) &&
	cat <<-EOF >expected &&
	error: could not revert $dream_oid... add dream
	hint: After resolving the conflicts, mark them with
	hint: "but add/rm <pathspec>", then run
	hint: "but revert --continue".
	hint: You can instead skip this cummit with "but revert --skip".
	hint: To abort and get back to the state before "but revert",
	hint: run "but revert --abort".
	EOF
	test_cummit --append --no-tag "double-add dream" dream dream &&
	test_must_fail but revert HEAD^ 2>actual &&
	test_cmp expected actual
'
test_done
