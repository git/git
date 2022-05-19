#!/bin/sh

test_description='but apply --3way'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

print_sanitized_conflicted_diff () {
	but diff HEAD >diff.raw &&
	sed -e '
		/^index /d
		s/^\(+[<>|][<>|][<>|][<>|]*\) .*/\1/
	' diff.raw
}

test_expect_success setup '
	test_tick &&
	test_write_lines 1 2 3 4 5 6 7 >one &&
	cat one >two &&
	but add one two &&
	but cummit -m initial &&

	but branch side &&

	test_tick &&
	test_write_lines 1 two 3 4 5 six 7 >one &&
	test_write_lines 1 two 3 4 5 6 7 >two &&
	but cummit -a -m main &&

	but checkout side &&
	test_write_lines 1 2 3 4 five 6 7 >one &&
	test_write_lines 1 2 3 4 five 6 7 >two &&
	but cummit -a -m side &&

	but checkout main
'

test_expect_success 'apply without --3way' '
	but diff side^ side >P.diff &&

	# should fail to apply
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but apply --index P.diff &&
	# should leave things intact
	but diff-files --exit-code &&
	but diff-index --exit-code --cached HEAD
'

test_apply_with_3way () {
	# Merging side should be similar to applying this patch
	but diff ...side >P.diff &&

	# The corresponding conflicted merge
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but merge --no-cummit side &&
	but ls-files -s >expect.ls &&
	print_sanitized_conflicted_diff >expect.diff &&

	# should fail to apply
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but apply --index --3way P.diff &&
	but ls-files -s >actual.ls &&
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
	but diff ...side >P.diff &&

	# The corresponding conflicted merge
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but merge --no-cummit side &&

	# Manually resolve and record the resolution
	test_write_lines 1 two 3 4 five six 7 >one &&
	but rerere &&
	cat one >expect &&

	# should fail to apply
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but apply --index --3way P.diff &&

	# but rerere should have replayed the recorded resolution
	test_cmp expect one
'

test_expect_success 'apply -3 with add/add conflict setup' '
	but reset --hard &&

	but checkout -b adder &&
	test_write_lines 1 2 3 4 5 6 7 >three &&
	test_write_lines 1 2 3 4 5 6 7 >four &&
	but add three four &&
	but cummit -m "add three and four" &&

	but checkout -b another adder^ &&
	test_write_lines 1 2 3 4 5 6 7 >three &&
	test_write_lines 1 2 3 four 5 6 7 >four &&
	but add three four &&
	but cummit -m "add three and four" &&

	# Merging another should be similar to applying this patch
	but diff adder...another >P.diff &&

	but checkout adder^0 &&
	test_must_fail but merge --no-cummit another &&
	but ls-files -s >expect.ls &&
	print_sanitized_conflicted_diff >expect.diff
'

test_expect_success 'apply -3 with add/add conflict' '
	# should fail to apply ...
	but reset --hard &&
	but checkout adder^0 &&
	test_must_fail but apply --index --3way P.diff &&
	# ... and leave conflicts in the index and in the working tree
	but ls-files -s >actual.ls &&
	print_sanitized_conflicted_diff >actual.diff &&

	# The result should resemble the corresponding merge
	test_cmp expect.ls actual.ls &&
	test_cmp expect.diff actual.diff
'

test_expect_success 'apply -3 with add/add conflict (dirty working tree)' '
	# should fail to apply ...
	but reset --hard &&
	but checkout adder^0 &&
	echo >>four &&
	cat four >four.save &&
	cat three >three.save &&
	but ls-files -s >expect.ls &&
	test_must_fail but apply --index --3way P.diff &&
	# ... and should not touch anything
	but ls-files -s >actual.ls &&
	test_cmp expect.ls actual.ls &&
	test_cmp four.save four &&
	test_cmp three.save three
'

test_expect_success 'apply -3 with ambiguous repeating file' '
	but reset --hard &&
	test_write_lines 1 2 1 2 1 2 1 2 1 2 1 >one_two_repeat &&
	but add one_two_repeat &&
	but cummit -m "init one" &&
	test_write_lines 1 2 1 2 1 2 1 2 one 2 1 >one_two_repeat &&
	but cummit -a -m "change one" &&

	but diff HEAD~ >Repeat.diff &&
	but reset --hard HEAD~ &&

	test_write_lines 1 2 1 2 1 2 one 2 1 2 one >one_two_repeat &&
	but cummit -a -m "change surrounding one" &&

	but apply --index --3way Repeat.diff &&
	test_write_lines 1 2 1 2 1 2 one 2 one 2 one >expect &&

	test_cmp expect one_two_repeat
'

test_expect_success 'apply with --3way --cached clean apply' '
	# Merging side should be similar to applying this patch
	but diff ...side >P.diff &&

	# The corresponding cleanly applied merge
	but reset --hard &&
	but checkout main~ &&
	but merge --no-cummit side &&
	but ls-files -s >expect.ls &&

	# should succeed
	but reset --hard &&
	but checkout main~ &&
	but apply --cached --3way P.diff &&
	but ls-files -s >actual.ls &&
	print_sanitized_conflicted_diff >actual.diff &&

	# The cache should resemble the corresponding merge
	# (both files at stage #0)
	test_cmp expect.ls actual.ls &&
	# However the working directory should not change
	>expect.diff &&
	test_cmp expect.diff actual.diff
'

test_expect_success 'apply with --3way --cached and conflicts' '
	# Merging side should be similar to applying this patch
	but diff ...side >P.diff &&

	# The corresponding conflicted merge
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but merge --no-cummit side &&
	but ls-files -s >expect.ls &&

	# should fail to apply
	but reset --hard &&
	but checkout main^0 &&
	test_must_fail but apply --cached --3way P.diff &&
	but ls-files -s >actual.ls &&
	print_sanitized_conflicted_diff >actual.diff &&

	# The cache should resemble the corresponding merge
	# (one file at stage #0, one file at stages #1 #2 #3)
	test_cmp expect.ls actual.ls &&
	# However the working directory should not change
	>expect.diff &&
	test_cmp expect.diff actual.diff
'

test_expect_success 'apply binary file patch' '
	but reset --hard main &&
	cp "$TEST_DIRECTORY/test-binary-1.png" bin.png &&
	but add bin.png &&
	but cummit -m "add binary file" &&

	cp "$TEST_DIRECTORY/test-binary-2.png" bin.png &&

	but diff --binary >bin.diff &&
	but reset --hard &&

	# Apply must succeed.
	but apply bin.diff
'

test_expect_success 'apply binary file patch with 3way' '
	but reset --hard main &&
	cp "$TEST_DIRECTORY/test-binary-1.png" bin.png &&
	but add bin.png &&
	but cummit -m "add binary file" &&

	cp "$TEST_DIRECTORY/test-binary-2.png" bin.png &&

	but diff --binary >bin.diff &&
	but reset --hard &&

	# Apply must succeed.
	but apply --3way --index bin.diff
'

test_expect_success 'apply full-index patch with 3way' '
	but reset --hard main &&
	cp "$TEST_DIRECTORY/test-binary-1.png" bin.png &&
	but add bin.png &&
	but cummit -m "add binary file" &&

	cp "$TEST_DIRECTORY/test-binary-2.png" bin.png &&

	but diff --full-index >bin.diff &&
	but reset --hard &&

	# Apply must succeed.
	but apply --3way --index bin.diff
'

test_expect_success 'apply delete then new patch with 3way' '
	but reset --hard main &&
	test_write_lines 2 > delnew &&
	but add delnew &&
	but diff --cached >> new.patch &&
	but reset --hard &&
	test_write_lines 1 > delnew &&
	but add delnew &&
	but cummit -m "delnew" &&
	rm delnew &&
	but diff >> delete-then-new.patch &&
	cat new.patch >> delete-then-new.patch &&

	but checkout -- . &&
	# Apply must succeed.
	but apply --3way delete-then-new.patch
'

test_done
