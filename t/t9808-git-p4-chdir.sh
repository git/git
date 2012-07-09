#!/bin/sh

test_description='git p4 relative chdir'

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

# P4 reads from P4CONFIG file to find its server params, if the
# environment variable is set
test_expect_success 'P4CONFIG and absolute dir clone' '
	printf "P4PORT=$P4PORT\nP4CLIENT=$P4CLIENT\n" >p4config &&
	test_when_finished "rm p4config" &&
	test_when_finished cleanup_git &&
	(
		P4CONFIG=p4config && export P4CONFIG &&
		sane_unset P4PORT P4CLIENT &&
		git p4 clone --verbose --dest="$git" //depot
	)
'

# same thing, but with relative directory name, note missing $ on --dest
test_expect_success 'P4CONFIG and relative dir clone' '
	printf "P4PORT=$P4PORT\nP4CLIENT=$P4CLIENT\n" >p4config &&
	test_when_finished "rm p4config" &&
	test_when_finished cleanup_git &&
	(
		P4CONFIG=p4config && export P4CONFIG &&
		sane_unset P4PORT P4CLIENT &&
		git p4 clone --verbose --dest="git" //depot
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
