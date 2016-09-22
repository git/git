#!/bin/sh

test_description='git p4 retrieve job info'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'add p4 jobs' '
	(
		p4_add_job TESTJOB-A &&
		p4_add_job TESTJOB-B
	)
'

test_expect_success 'add p4 files' '
	client_view "//depot/... //client/..." &&
	(
		cd "$cli" &&
		>file1 &&
		p4 add file1 &&
		p4 submit -d "Add file 1"
	)
'

test_expect_success 'check log message of changelist with no jobs' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git p4 clone --use-client-spec --destination="$git" //depot@all &&
		cat >expect <<-\EOF &&
		Add file 1
		[git-p4: depot-paths = "//depot/": change = 1]

		EOF
		git log --format=%B >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add TESTJOB-A to change 1' '
	(
		cd "$cli" &&
		p4 fix -c 1 TESTJOB-A
	)
'

test_expect_success 'check log message of changelist with one job' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git p4 clone --use-client-spec --destination="$git" //depot@all &&
		cat >expect <<-\EOF &&
		Add file 1
		Jobs: TESTJOB-A
		[git-p4: depot-paths = "//depot/": change = 1]

		EOF
		git log --format=%B >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add TESTJOB-B to change 1' '
	(
		cd "$cli" &&
		p4 fix -c 1 TESTJOB-B
	)
'

test_expect_success 'check log message of changelist with more jobs' '
	client_view "//depot/... //client/..." &&
	test_when_finished cleanup_git &&
	(
		cd "$git" &&
		git init . &&
		git p4 clone --use-client-spec --destination="$git" //depot@all &&
		cat >expect <<-\EOF &&
		Add file 1
		Jobs: TESTJOB-A TESTJOB-B
		[git-p4: depot-paths = "//depot/": change = 1]

		EOF
		git log --format=%B >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'

test_done
