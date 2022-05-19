#!/bin/sh

test_description='but p4 with server triggers'

. ./lib-but-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'init depot' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "change 1" &&
		echo file2 >file2 &&
		p4 add file2 &&
		p4 submit -d "change 2"
	)
'

test_expect_success 'clone with extra info lines from verbose p4 trigger' '
	test_when_finished cleanup_but &&
	(
		p4 triggers -i <<-EOF
		Triggers: p4triggertest-command command pre-user-change "echo verbose trigger"
		EOF
	) &&
	(
		p4 change -o |  grep -s "verbose trigger"
	) &&
	but p4 clone --dest="$but" //depot/@all &&
	(
		p4 triggers -i <<-EOF
		Triggers:
		EOF
	)
'

test_expect_success 'import with extra info lines from verbose p4 trigger' '
	test_when_finished cleanup_but &&
	(
		cd "$cli" &&
		echo file3 >file3 &&
		p4 add file3 &&
		p4 submit -d "change 3"
	) &&
	(
		p4 triggers -i <<-EOF
		Triggers: p4triggertest-command command pre-user-describe "echo verbose trigger"
		EOF
	) &&
	(
		p4 describe 1 |  grep -s "verbose trigger"
	) &&
	but p4 clone --dest="$but" //depot/@all &&
	(
		cd "$but" &&
		but p4 sync
	) &&
	(
		p4 triggers -i <<-EOF
		Triggers:
		EOF
	)
'

test_expect_success 'submit description with extra info lines from verbose p4 change trigger' '
	test_when_finished cleanup_but &&
	(
		p4 triggers -i <<-EOF
		Triggers: p4triggertest-command command pre-user-change "echo verbose trigger"
		EOF
	) &&
	(
		p4 change -o |  grep -s "verbose trigger"
	) &&
	but p4 clone --dest="$but" //depot &&
	(
		cd "$but" &&
		but config but-p4.skipSubmitEdit true &&
		echo file4 >file4 &&
		but add file4 &&
		but cummit -m file4 &&
		but p4 submit
	) &&
	(
		p4 triggers -i <<-EOF
		Triggers:
		EOF
	) &&
	(
		cd "$cli" &&
		test_path_is_file file4
	)
'

test_done
