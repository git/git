#!/bin/sh

test_description='git p4 errors'

. ./lib-git-p4.sh

test_expect_success 'start p4d' '
	start_p4d
'

test_expect_success 'add p4 files' '
	(
		cd "$cli" &&
		echo file1 >file1 &&
		p4 add file1 &&
		p4 submit -d "file1"
	)
'

# after this test, the default user requires a password
test_expect_success 'error handling' '
	git p4 clone --dest="$git" //depot@all &&
	(
		cd "$git" &&
		P4PORT=: test_must_fail git p4 submit 2>errmsg
	) &&
	p4 passwd -P newpassword &&
	(
		P4PASSWD=badpassword &&
		export P4PASSWD &&
		test_must_fail git p4 clone //depot/foo 2>errmsg &&
		grep -q "failure accessing depot.*P4PASSWD" errmsg
	)
'

test_expect_success 'ticket logged out' '
	P4TICKETS="$cli/tickets" &&
	echo "newpassword" | p4 login &&
	(
		cd "$git" &&
		test_commit "ticket-auth-check" &&
		p4 logout &&
		test_must_fail git p4 submit 2>errmsg &&
		grep -q "failure accessing depot" errmsg
	)
'

test_expect_success 'create group with short ticket expiry' '
	P4TICKETS="$cli/tickets" &&
	echo "newpassword" | p4 login &&
	p4_add_user short_expiry_user &&
	p4 -u short_expiry_user passwd -P password &&
	p4 group -i <<-EOF &&
	Group: testgroup
	Timeout: 3
	Users: short_expiry_user
	EOF

	p4 users | grep short_expiry_user
'

test_expect_success 'git operation with expired ticket' '
	P4TICKETS="$cli/tickets" &&
	P4USER=short_expiry_user &&
	echo "password" | p4 login &&
	(
		cd "$git" &&
		git p4 sync &&
		sleep 5 &&
		test_must_fail git p4 sync 2>errmsg &&
		grep "failure accessing depot" errmsg
	)
'

test_expect_success 'kill p4d' '
	kill_p4d
'


test_done
