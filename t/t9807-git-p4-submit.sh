#!/bin/sh

test_description='git p4 submit'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "change 1"
	)
'

test_expect_success 'submit with no client dir' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo file2 >file2 &&
		git add file2 &&
		git commit -m "git commit 2" &&
		rm -rf "$cli" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file1 &&
		test_path_is_file file2
	)
'

# make two commits, but tell it to apply only from HEAD^
test_expect_success 'submit --origin' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_commit "file3" &&
		test_commit "file4" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit --origin=HEAD^
	) &&
	(
		cd "$cli" &&
		test_path_is_missing "file3.t" &&
		test_path_is_file "file4.t"
	)
'

test_expect_success 'submit with allowSubmit' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_commit "file5" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.allowSubmit "nobranch" &&
		test_must_fail git p4 submit &&
		git config git-p4.allowSubmit "nobranch,master" &&
		git p4 submit
	)
'

test_expect_success 'submit with master branch name from argv' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		test_commit "file6" &&
		git config git-p4.skipSubmitEdit true &&
		test_must_fail git p4 submit nobranch &&
		git branch otherbranch &&
		git reset --hard HEAD^ &&
		test_commit "file7" &&
		git p4 submit otherbranch
	) &&
	(
		cd "$cli" &&
		test_path_is_file "file6.t" &&
		test_path_is_missing "file7.t"
	)
'

#
# Basic submit tests, the five handled cases
#

test_expect_success 'submit modify' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		echo line >>file1 &&
		git add file1 &&
		git commit -m file1 &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file1 &&
		test_line_count = 2 file1
	)
'

test_expect_success 'submit add' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		echo file13 >file13 &&
		git add file13 &&
		git commit -m file13 &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file13
	)
'

test_expect_success 'submit delete' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git rm file4.t &&
		git commit -m "delete file4.t" &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing file4.t
	)
'

test_expect_success 'submit copy' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.detectCopies true &&
		git config git-p4.detectCopiesHarder true &&
		cp file5.t file5.ta &&
		git add file5.ta &&
		git commit -m "copy to file5.ta" &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file file5.ta &&
		test ! -w file5.ta
	)
'

test_expect_success 'submit rename' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		git config git-p4.detectRenames true &&
		git mv file6.t file6.ta &&
		git commit -m "rename file6.t to file6.ta" &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing file6.t &&
		test_path_is_file file6.ta &&
		test ! -w file6.ta
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
