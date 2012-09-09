#!/bin/sh

test_description='git p4 submit failure handling'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		p4 client -o | sed "/LineEnd/s/:.*/:unix/" | p4 client -i &&
		echo line1 >file1 &&
		p4 add file1 &&
		p4 submit -d "line1 in file1"
	)
'

test_expect_success 'conflict on one commit' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line2 >>file1 &&
		p4 submit -d "line2 in file1"
	) &&
	(
		# now this commit should cause a conflict
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		echo line3 >>file1 &&
		git add file1 &&
		git commit -m "line3 in file1 will conflict" &&
		test_expect_code 1 git p4 submit >out &&
		test_i18ngrep "No commits applied" out
	)
'

test_expect_success 'conflict on second of two commits' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line3 >>file1 &&
		p4 submit -d "line3 in file1"
	) &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		# this commit is okay
		test_commit "first_commit_okay" &&
		# now this submit should cause a conflict
		echo line4 >>file1 &&
		git add file1 &&
		git commit -m "line4 in file1 will conflict" &&
		test_expect_code 1 git p4 submit >out &&
		test_i18ngrep "Applied only the commits" out
	)
'

test_expect_success 'conflict on first of two commits, skip' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line4 >>file1 &&
		p4 submit -d "line4 in file1"
	) &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		# this submit should cause a conflict
		echo line5 >>file1 &&
		git add file1 &&
		git commit -m "line5 in file1 will conflict" &&
		# but this commit is okay
		test_commit "okay_commit_after_skip" &&
		echo s | test_expect_code 1 git p4 submit >out &&
		test_i18ngrep "Applied only the commits" out
	)
'

test_expect_success 'conflict on first of two commits, quit' '
	test_when_finished cleanup_git &&
	git p4 clone --dest="$git" //depot &&
	(
		cd "$cli" &&
		p4 open file1 &&
		echo line7 >>file1 &&
		p4 submit -d "line7 in file1"
	) &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		# this submit should cause a conflict
		echo line8 >>file1 &&
		git add file1 &&
		git commit -m "line8 in file1 will conflict" &&
		# but this commit is okay
		test_commit "okay_commit_after_quit" &&
		echo q | test_expect_code 1 git p4 submit >out &&
		test_i18ngrep "No commits applied" out
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
