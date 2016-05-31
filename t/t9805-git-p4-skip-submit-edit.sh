#!/bin/sh

test_description='git p4 skipSubmitEdit config variables'

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

# this works because P4EDITOR is set to true
test_expect_success 'no config, unedited, say yes' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		echo line >>file1 &&
		git commit -a -m "change 2" &&
		echo y | git p4 submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 2 wc
	)
'

test_expect_success 'no config, unedited, say no' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		echo line >>file1 &&
		git commit -a -m "change 3 (not really)" &&
		printf "bad response\nn\n" | test_expect_code 1 git p4 submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 2 wc
	)
'

test_expect_success 'skipSubmitEdit' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		# will fail if editor is even invoked
		git config core.editor /bin/false &&
		echo line >>file1 &&
		git commit -a -m "change 3" &&
		git p4 submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 3 wc
	)
'

test_expect_success 'skipSubmitEditCheck' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		echo line >>file1 &&
		git commit -a -m "change 4" &&
		git p4 submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 4 wc
	)
'

# check the normal case, where the template really is edited
test_expect_success 'no config, edited' '
	git p4 clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	test_when_finished "rm ed.sh" &&
	cat >ed.sh <<-EOF &&
		#!$SHELL_PATH
		sleep 1
		touch "\$1"
		exit 0
	EOF
	chmod 755 ed.sh &&
	(
		cd "$git" &&
		echo line >>file1 &&
		git commit -a -m "change 5" &&
		P4EDITOR="\"$TRASH_DIRECTORY/ed.sh\"" &&
		export P4EDITOR &&
		git p4 submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 5 wc
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
