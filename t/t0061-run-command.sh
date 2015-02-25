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

test_done
