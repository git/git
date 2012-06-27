#!/bin/sh

test_description='git p4 wildcards'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'add p4 files with wildcards in the names' '
	(
		cd "$cli" &&
		printf "file2\nhas\nsome\nrandom\ntext\n" >file2 &&
		p4 add file2 &&
		echo file-wild-hash >file-wild#hash &&
		echo file-wild-star >file-wild\*star &&
		echo file-wild-at >file-wild@at &&
		echo file-wild-percent >file-wild%percent &&
		p4 add -f file-wild* &&
		p4 submit -d "file wildcards"
	)
'

test_expect_success 'wildcard files git p4 clone' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		test -f file-wild#hash &&
		test -f file-wild\*star &&
		test -f file-wild@at &&
		test -f file-wild%percent
	)
'

test_expect_success 'wildcard files submit back to p4, add' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo git-wild-hash >git-wild#hash &&
		echo git-wild-star >git-wild\*star &&
		echo git-wild-at >git-wild@at &&
		echo git-wild-percent >git-wild%percent &&
		git add git-wild* &&
		git commit -m "add some wildcard filenames" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file git-wild#hash &&
		test_path_is_file git-wild\*star &&
		test_path_is_file git-wild@at &&
		test_path_is_file git-wild%percent
	)
'

test_expect_success 'wildcard files submit back to p4, modify' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		echo new-line >>git-wild#hash &&
		echo new-line >>git-wild\*star &&
		echo new-line >>git-wild@at &&
		echo new-line >>git-wild%percent &&
		git add git-wild* &&
		git commit -m "modify the wildcard files" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_line_count = 2 git-wild#hash &&
		test_line_count = 2 git-wild\*star &&
		test_line_count = 2 git-wild@at &&
		test_line_count = 2 git-wild%percent
	)
'

test_expect_success 'wildcard files submit back to p4, copy' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		cp file2 git-wild-cp#hash &&
		git add git-wild-cp#hash &&
		cp git-wild\*star file-wild-3 &&
		git add file-wild-3 &&
		git commit -m "wildcard copies" &&
		git config git-p4.detectCopies true &&
		git config git-p4.detectCopiesHarder true &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_file git-wild-cp#hash &&
		test_path_is_file file-wild-3
	)
'

test_expect_success 'wildcard files submit back to p4, rename' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git mv git-wild@at file-wild-4 &&
		git mv file-wild-3 git-wild-cp%percent &&
		git commit -m "wildcard renames" &&
		git config git-p4.detectRenames true &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing git-wild@at &&
		test_path_is_file git-wild-cp%percent
	)
'

test_expect_success 'wildcard files submit back to p4, delete' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$git" &&
		git rm git-wild* &&
		git commit -m "delete the wildcard files" &&
		git config git-p4.skipSubmitEdit true &&
		git p4 submit
	) &&
	(
		cd "$cli" &&
		test_path_is_missing git-wild#hash &&
		test_path_is_missing git-wild\*star &&
		test_path_is_missing git-wild@at &&
		test_path_is_missing git-wild%percent
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
