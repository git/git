#!/bin/sh

test_description='git apply --3way'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
	git commit -a -m main &&

	git checkout side &&
	test_write_lines 1 2 3 4 five 6 7 >one &&
	test_write_lines 1 2 3 4 five 6 7 >two &&
	git commit -a -m side &&

	git checkout main
'

test_expect_success 'apply without --3way' '
	git diff side^ side >P.diff &&

	# should fail to apply
	git reset --hard &&
	git checkout main^0 &&
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
	git checkout main^0 &&
	test_must_fail git merge --no-commit side &&
	git ls-files -s >expect.ls &&
	print_sanitized_conflicted_diff >expect.diff &&

	# should fail to apply
	git reset --hard &&
	git checkout main^0 &&
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
	git checkout main^0 &&
	test_must_fail git merge --no-commit side &&

	# Manually resolve and record the resolution
	test_write_lines 1 two 3 4 five six 7 >one &&
	git rerere &&
	cat one >expect &&

	# should fail to apply
	git reset --hard &&
	git checkout main^0 &&
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

test_expect_success 'apply -3 with ambiguous repeating file' '
	git reset --hard &&
	test_write_lines 1 2 1 2 1 2 1 2 1 2 1 >one_two_repeat &&
	git add one_two_repeat &&
	git commit -m "init one" &&
	test_write_lines 1 2 1 2 1 2 1 2 one 2 1 >one_two_repeat &&
	git commit -a -m "change one" &&

	git diff HEAD~ >Repeat.diff &&
	git reset --hard HEAD~ &&

	test_write_lines 1 2 1 2 1 2 one 2 1 2 one >one_two_repeat &&
	git commit -a -m "change surrounding one" &&

	git apply --index --3way Repeat.diff &&
	test_write_lines 1 2 1 2 1 2 one 2 one 2 one >expect &&

	test_cmp expect one_two_repeat
'

test_expect_success 'apply with --3way --cached clean apply' '
	# Merging side should be similar to applying this patch
	git diff ...side >P.diff &&

	# The corresponding cleanly applied merge
	git reset --hard &&
	git checkout main~ &&
	git merge --no-commit side &&
	git ls-files -s >expect.ls &&

	# should succeed
	git reset --hard &&
	git checkout main~ &&
	git apply --cached --3way P.diff &&
	git ls-files -s >actual.ls &&
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
	git diff ...side >P.diff &&

	# The corresponding conflicted merge
	git reset --hard &&
	git checkout main^0 &&
	test_must_fail git merge --no-commit side &&
	git ls-files -s >expect.ls &&

	# should fail to apply
	git reset --hard &&
	git checkout main^0 &&
	test_must_fail git apply --cached --3way P.diff &&
	git ls-files -s >actual.ls &&
	print_sanitized_conflicted_diff >actual.diff &&

	# The cache should resemble the corresponding merge
	# (one file at stage #0, one file at stages #1 #2 #3)
	test_cmp expect.ls actual.ls &&
	# However the working directory should not change
	>expect.diff &&
	test_cmp expect.diff actual.diff
'

test_expect_success 'apply binary file patch' '
	git reset --hard main &&
	cp "$TEST_DIRECTORY/test-binary-1.png" bin.png &&
	git add bin.png &&
	git commit -m "add binary file" &&

	cp "$TEST_DIRECTORY/test-binary-2.png" bin.png &&

	git diff --binary >bin.diff &&
	git reset --hard &&

	# Apply must succeed.
	git apply bin.diff
'

test_expect_success 'apply binary file patch with 3way' '
	git reset --hard main &&
	cp "$TEST_DIRECTORY/test-binary-1.png" bin.png &&
	git add bin.png &&
	git commit -m "add binary file" &&

	cp "$TEST_DIRECTORY/test-binary-2.png" bin.png &&

	git diff --binary >bin.diff &&
	git reset --hard &&

	# Apply must succeed.
	git apply --3way --index bin.diff
'

test_expect_success 'apply full-index patch with 3way' '
	git reset --hard main &&
	cp "$TEST_DIRECTORY/test-binary-1.png" bin.png &&
	git add bin.png &&
	git commit -m "add binary file" &&

	cp "$TEST_DIRECTORY/test-binary-2.png" bin.png &&

	git diff --full-index >bin.diff &&
	git reset --hard &&

	# Apply must succeed.
	git apply --3way --index bin.diff
'

test_expect_success 'apply delete then new patch with 3way' '
	git reset --hard main &&
	test_write_lines 2 > delnew &&
	git add delnew &&
	git diff --cached >> new.patch &&
	git reset --hard &&
	test_write_lines 1 > delnew &&
	git add delnew &&
	git commit -m "delnew" &&
	rm delnew &&
	git diff >> delete-then-new.patch &&
	cat new.patch >> delete-then-new.patch &&

	git checkout -- . &&
	# Apply must succeed.
	git apply --3way delete-then-new.patch
'

test_done
