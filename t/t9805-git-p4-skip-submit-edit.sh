#!/bin/sh

test_description='git-p4 skipSubmitEdit config variables'

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

# this works because EDITOR is set to :
test_expect_success 'no config, unedited, say yes' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		echo line >>file1 &&
		git commit -a -m "change 2" &&
		echo y | "$GITP4" submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 2 wc
	)
'

test_expect_success 'no config, unedited, say no' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		echo line >>file1 &&
		git commit -a -m "change 3 (not really)" &&
		printf "bad response\nn\n" | "$GITP4" submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 2 wc
	)
'

test_expect_success 'skipSubmitEdit' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEdit true &&
		# will fail if editor is even invoked
		git config core.editor /bin/false &&
		echo line >>file1 &&
		git commit -a -m "change 3" &&
		"$GITP4" submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 3 wc
	)
'

test_expect_success 'skipSubmitEditCheck' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git config git-p4.skipSubmitEditCheck true &&
		echo line >>file1 &&
		git commit -a -m "change 4" &&
		"$GITP4" submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 4 wc
	)
'

# check the normal case, where the template really is edited
test_expect_success 'no config, edited' '
	"$GITP4" clone --dest="$git" //depot &&
	test_when_finished cleanup_git &&
	ed="$TRASH_DIRECTORY/ed.sh" &&
	test_when_finished "rm \"$ed\"" &&
	cat >"$ed" <<-EOF &&
		#!$SHELL_PATH
		sleep 1
		touch "\$1"
		exit 0
	EOF
	chmod 755 "$ed" &&
	(
		cd "$git" &&
		echo line >>file1 &&
		git commit -a -m "change 5" &&
		EDITOR="\"$ed\"" "$GITP4" submit &&
		p4 changes //depot/... >wc &&
		test_line_count = 5 wc
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
