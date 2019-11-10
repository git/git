#!/bin/sh

test_description='git apply --3way'

. ./test-lib.sh

print_sanitized_conflicted_diff () {
	git diff HEAD >diff.raw &&
	sed -e '
		/^index /d
		s/^\(+[<>|][<>|][<>|][<>|]*\) .*/\1/
	' diff.raw
}

test_expect_success setup '
	test_tick &&
	test_write_lines 1 2 3 4 5 6 7 >one &&
	cat one >two &&
	git add one two &&
	git commit -m initial &&

	git branch side &&

	test_tick &&
	test_write_lines 1 two 3 4 5 six 7 >one &&
	test_write_lines 1 two 3 4 5 6 7 >two &&
	git commit -a -m master &&

	git checkout side &&
	test_write_lines 1 2 3 4 five 6 7 >one &&
	test_write_lines 1 2 3 4 five 6 7 >two &&
	git commit -a -m side &&

	git checkout master
'

test_expect_success 'apply without --3way' '
	git diff side^ side >P.diff &&

	# should fail to apply
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git apply --index P.diff &&
	# should leave things intact
	git diff-files --exit-code &&
	git diff-index --exit-code --cached HEAD
'

test_apply_with_3way () {
	# Merging side should be similar to applying this patch
	git diff ...side >P.diff &&

	# The corresponding conflicted merge
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git merge --no-commit side &&
	git ls-files -s >expect.ls &&
	print_sanitized_conflicted_diff >expect.diff &&

	# should fail to apply
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git apply --index --3way P.diff &&
	git ls-files -s >actual.ls &&
	print_sanitized_conflicted_diff >actual.diff &&

	# The result should resemble the corresponding merge
	test_cmp expect.ls actual.ls &&
	test_cmp expect.diff actual.diff
}

test_expect_success 'apply with --3way' '
	test_apply_with_3way
'

test_expect_success 'apply with --3way with merge.conflictStyle = diff3' '
	test_config merge.conflictStyle diff3 &&
	test_apply_with_3way
'

test_expect_success 'apply with --3way with rerere enabled' '
	test_config rerere.enabled true &&

	# Merging side should be similar to applying this patch
	git diff ...side >P.diff &&

	# The corresponding conflicted merge
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git merge --no-commit side &&

	# Manually resolve and record the resolution
	test_write_lines 1 two 3 4 five six 7 >one &&
	git rerere &&
	cat one >expect &&

	# should fail to apply
	git reset --hard &&
	git checkout master^0 &&
	test_must_fail git apply --index --3way P.diff &&

	# but rerere should have replayed the recorded resolution
	test_cmp expect one
'

test_expect_success 'apply -3 with add/add conflict setup' '
	git reset --hard &&

	git checkout -b adder &&
	test_write_lines 1 2 3 4 5 6 7 >three &&
	test_write_lines 1 2 3 4 5 6 7 >four &&
	git add three four &&
	git commit -m "add three and four" &&

	git checkout -b another adder^ &&
	test_write_lines 1 2 3 4 5 6 7 >three &&
	test_write_lines 1 2 3 four 5 6 7 >four &&
	git add three four &&
	git commit -m "add three and four" &&

	# Merging another should be similar to applying this patch
	git diff adder...another >P.diff &&

	git checkout adder^0 &&
	test_must_fail git merge --no-commit another &&
	git ls-files -s >expect.ls &&
	print_sanitized_conflicted_diff >expect.diff
'

test_expect_success 'apply -3 with add/add conflict' '
	# should fail to apply ...
	git reset --hard &&
	git checkout adder^0 &&
	test_must_fail git apply --index --3way P.diff &&
	# ... and leave conflicts in the index and in the working tree
	git ls-files -s >actual.ls &&
	print_sanitized_conflicted_diff >actual.diff &&

	# The result should resemble the corresponding merge
	test_cmp expect.ls actual.ls &&
	test_cmp expect.diff actual.diff
'

test_expect_success 'apply -3 with add/add conflict (dirty working tree)' '
	# should fail to apply ...
	git reset --hard &&
	git checkout adder^0 &&
	echo >>four &&
	cat four >four.save &&
	cat three >three.save &&
	git ls-files -s >expect.ls &&
	test_must_fail git apply --index --3way P.diff &&
	# ... and should not touch anything
	git ls-files -s >actual.ls &&
	test_cmp expect.ls actual.ls &&
	test_cmp four.save four &&
	test_cmp three.save three
'

test_done
