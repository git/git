#!/bin/sh
#
#

test_description='git status collapse ignored'

. ./test-lib.sh


cat >.gitignore <<\EOF
*.ign
ignored_dir/
!*.unignore
EOF

# commit initial ignore file
test_expect_success 'setup initial commit and ignore file' '
	git add . &&
	test_tick &&
	git commit -m "Initial commit"
'

cat >expect <<\EOF
? err
? expect
? output
! dir/ignored/ignored_1.ign
! dir/ignored/ignored_2.ign
! ignored/ignored_1.ign
! ignored/ignored_2.ign
EOF

# Test status behavior on folder with ignored files
test_expect_success 'setup folder with ignored files' '
	mkdir -p ignored dir/ignored &&
	touch ignored/ignored_1.ign ignored/ignored_2.ign \
		dir/ignored/ignored_1.ign dir/ignored/ignored_2.ign
'

test_expect_success 'Verify behavior of status on folders with ignored files' '
	test_when_finished "git clean -fdx" &&
	git status --porcelain=v2 --ignored --untracked-files=all --show-ignored-directory >output 2>err &&
	test_i18ncmp expect output &&
	grep "deprecated.*use --ignored=matching instead" err
'

# Test status bahavior on folder with tracked and ignored files
cat >expect <<\EOF
? expect
? output
! dir/tracked_ignored/ignored_1.ign
! dir/tracked_ignored/ignored_2.ign
! tracked_ignored/ignored_1.ign
! tracked_ignored/ignored_2.ign
EOF

test_expect_success 'setup folder with tracked & ignored files' '
	mkdir -p tracked_ignored dir/tracked_ignored &&
	touch tracked_ignored/tracked_1 tracked_ignored/tracked_2 \
		tracked_ignored/ignored_1.ign tracked_ignored/ignored_2.ign \
		dir/tracked_ignored/tracked_1 dir/tracked_ignored/tracked_2 \
		dir/tracked_ignored/ignored_1.ign dir/tracked_ignored/ignored_2.ign &&

	git add tracked_ignored/tracked_1 tracked_ignored/tracked_2 \
		dir/tracked_ignored/tracked_1 dir/tracked_ignored/tracked_2 &&
	test_tick &&
	git commit -m "commit tracked files"
'

test_expect_success 'Verify status on folder with tracked & ignored files' '
	test_when_finished "git clean -fdx && git reset HEAD~1 --hard" &&
	git status --porcelain=v2 --ignored --untracked-files=all --show-ignored-directory >output &&
	test_i18ncmp expect output
'


# Test status behavior on folder with untracked and ignored files
cat >expect <<\EOF
? dir/untracked_ignored/untracked_1
? dir/untracked_ignored/untracked_2
? expect
? output
? untracked_ignored/untracked_1
? untracked_ignored/untracked_2
! dir/untracked_ignored/ignored_1.ign
! dir/untracked_ignored/ignored_2.ign
! untracked_ignored/ignored_1.ign
! untracked_ignored/ignored_2.ign
EOF

test_expect_success 'setup folder with tracked & ignored files' '
	mkdir -p untracked_ignored dir/untracked_ignored &&
	touch untracked_ignored/untracked_1 untracked_ignored/untracked_2 \
		untracked_ignored/ignored_1.ign untracked_ignored/ignored_2.ign \
		dir/untracked_ignored/untracked_1 dir/untracked_ignored/untracked_2 \
		dir/untracked_ignored/ignored_1.ign dir/untracked_ignored/ignored_2.ign
'

test_expect_success 'Verify status on folder with tracked & ignored files' '
	test_when_finished "git clean -fdx" &&
	git status --porcelain=v2 --ignored --untracked-files=all --show-ignored-directory >output &&
	test_i18ncmp expect output
'

# Test status behavior on ignored folder
cat >expect <<\EOF
? expect
? output
! ignored_dir/
EOF

test_expect_success 'setup folder with tracked & ignored files' '
	mkdir ignored_dir &&
	touch ignored_dir/ignored_1 ignored_dir/ignored_2 \
		ignored_dir/ignored_1.ign ignored_dir/ignored_2.ign
'

test_expect_success 'Verify status on folder with tracked & ignored files' '
	test_when_finished "git clean -fdx" &&
	git status --porcelain=v2 --ignored --untracked-files=all --show-ignored-directory >output &&
	test_i18ncmp expect output
'

# Test status behavior on ignored folder with tracked file
cat >expect <<\EOF
? expect
? output
! ignored_dir/ignored_1
! ignored_dir/ignored_1.ign
! ignored_dir/ignored_2
! ignored_dir/ignored_2.ign
EOF

test_expect_success 'setup folder with tracked & ignored files' '
	mkdir ignored_dir &&
	touch ignored_dir/ignored_1 ignored_dir/ignored_2 \
		ignored_dir/ignored_1.ign ignored_dir/ignored_2.ign \
		ignored_dir/tracked &&
	git add -f ignored_dir/tracked &&
	test_tick &&
	git commit -m "Force add file in ignored directory"
'

test_expect_success 'Verify status on folder with tracked & ignored files' '
	test_when_finished "git clean -fdx && git reset HEAD~1 --hard" &&
	git status --porcelain=v2 --ignored --untracked-files=all --show-ignored-directory >output &&
	test_i18ncmp expect output
'

test_done

