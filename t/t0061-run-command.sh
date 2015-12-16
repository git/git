#!/bin/sh
#
# Copyright (c) 2009 Ilari Liusvaara
#

test_description='Test run command'

. ./test-lib.sh

cat >hello-script <<-EOF
	#!$SHELL_PATH
	cat hello-script
EOF
>empty

test_expect_success 'start_command reports ENOENT' '
	test-run-command start-command-ENOENT ./does-not-exist
'

test_expect_success 'run_command can run a command' '
	cat hello-script >hello.sh &&
	chmod +x hello.sh &&
	test-run-command run-command ./hello.sh >actual 2>err &&

	test_cmp hello-script actual &&
	test_cmp empty err
'

test_expect_success POSIXPERM 'run_command reports EACCES' '
	cat hello-script >hello.sh &&
	chmod -x hello.sh &&
	test_must_fail test-run-command run-command ./hello.sh 2>err &&

	grep "fatal: cannot exec.*hello.sh" err
'

test_expect_success POSIXPERM,SANITY 'unreadable directory in PATH' '
	mkdir local-command &&
	test_when_finished "chmod u+rwx local-command && rm -fr local-command" &&
	git config alias.nitfol "!echo frotz" &&
	chmod a-rx local-command &&
	(
		PATH=./local-command:$PATH &&
		git nitfol >actual
	) &&
	echo frotz >expect &&
	test_cmp expect actual
'

cat >expect <<-EOF
preloaded output of a child
Hello
World
preloaded output of a child
Hello
World
preloaded output of a child
Hello
World
preloaded output of a child
Hello
World
EOF

test_expect_success 'run_command runs in parallel with more jobs available than tasks' '
	test-run-command run-command-parallel 5 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with as many jobs as tasks' '
	test-run-command run-command-parallel 4 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with more tasks than jobs available' '
	test-run-command run-command-parallel 3 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

cat >expect <<-EOF
preloaded output of a child
asking for a quick stop
preloaded output of a child
asking for a quick stop
preloaded output of a child
asking for a quick stop
EOF

test_expect_success 'run_command is asked to abort gracefully' '
	test-run-command run-command-abort 3 false 2>actual &&
	test_cmp expect actual
'

cat >expect <<-EOF
no further jobs available
EOF

test_expect_success 'run_command outputs ' '
	test-run-command run-command-no-jobs 3 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

test_done
