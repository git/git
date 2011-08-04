#!/bin/sh

test_description='Test interaction of reset --hard with sequencer

  + anotherpick: rewrites foo to d
  + picked: rewrites foo to c
  + unrelatedpick: rewrites unrelated to reallyunrelated
  + base: rewrites foo to b
  + initial: writes foo as a, unrelated as unrelated
'

. ./test-lib.sh

pristine_detach () {
	git cherry-pick --reset &&
	git checkout -f "$1^0" &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x
}

test_expect_success setup '
	echo unrelated >unrelated &&
	git add unrelated &&
	test_commit initial foo a &&
	test_commit base foo b &&
	test_commit unrelatedpick unrelated reallyunrelated &&
	test_commit picked foo c &&
	test_commit anotherpick foo d &&
	git config advice.detachedhead false

'

test_expect_success 'reset --hard cleans up sequencer state, providing one-level undo' '
	pristine_detach initial &&
	test_must_fail git cherry-pick base..anotherpick &&
	test_path_is_dir .git/sequencer &&
	git reset --hard &&
	test_path_is_missing .git/sequencer &&
	test_path_is_dir .git/sequencer-old &&
	git reset --hard &&
	test_path_is_missing .git/sequencer-old
'

test_done
