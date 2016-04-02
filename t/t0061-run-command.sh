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

test_expect_success 'start_command reports ENOENT (slash)' '
	test-tool run-command start-command-ENOENT ./does-not-exist 2>err &&
	test_i18ngrep "\./does-not-exist" err
'

test_expect_success 'start_command reports ENOENT (no slash)' '
	test-tool run-command start-command-ENOENT does-not-exist 2>err &&
	test_i18ngrep "does-not-exist" err
'

test_expect_success 'run_command can run a command' '
	cat hello-script >hello.sh &&
	chmod +x hello.sh &&
	test-tool run-command run-command ./hello.sh >actual 2>err &&

	test_cmp hello-script actual &&
	test_must_be_empty err
'


test_lazy_prereq RUNS_COMMANDS_FROM_PWD '
	write_script runs-commands-from-pwd <<-\EOF &&
	true
	EOF
	runs-commands-from-pwd >/dev/null 2>&1
'

test_expect_success !RUNS_COMMANDS_FROM_PWD 'run_command is restricted to PATH' '
	write_script should-not-run <<-\EOF &&
	echo yikes
	EOF
	test_must_fail test-tool run-command run-command should-not-run 2>err &&
	test_i18ngrep "should-not-run" err
'

test_expect_success !MINGW 'run_command can run a script without a #! line' '
	cat >hello <<-\EOF &&
	cat hello-script
	EOF
	chmod +x hello &&
	test-tool run-command run-command ./hello >actual 2>err &&

	test_cmp hello-script actual &&
	test_must_be_empty err
'

test_expect_success 'run_command does not try to execute a directory' '
	test_when_finished "rm -rf bin1 bin2" &&
	mkdir -p bin1/greet bin2 &&
	write_script bin2/greet <<-\EOF &&
	cat bin2/greet
	EOF

	PATH=$PWD/bin1:$PWD/bin2:$PATH \
		test-tool run-command run-command greet >actual 2>err &&
	test_cmp bin2/greet actual &&
	test_must_be_empty err
'

test_expect_success POSIXPERM 'run_command passes over non-executable file' '
	test_when_finished "rm -rf bin1 bin2" &&
	mkdir -p bin1 bin2 &&
	write_script bin1/greet <<-\EOF &&
	cat bin1/greet
	EOF
	chmod -x bin1/greet &&
	write_script bin2/greet <<-\EOF &&
	cat bin2/greet
	EOF

	PATH=$PWD/bin1:$PWD/bin2:$PATH \
		test-tool run-command run-command greet >actual 2>err &&
	test_cmp bin2/greet actual &&
	test_must_be_empty err
'

test_expect_success POSIXPERM 'run_command reports EACCES' '
	cat hello-script >hello.sh &&
	chmod -x hello.sh &&
	test_must_fail test-tool run-command run-command ./hello.sh 2>err &&

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
	test-tool run-command run-command-parallel 5 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with as many jobs as tasks' '
	test-tool run-command run-command-parallel 4 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with more tasks than jobs available' '
	test-tool run-command run-command-parallel 3 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
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
	test-tool run-command run-command-abort 3 false 2>actual &&
	test_cmp expect actual
'

cat >expect <<-EOF
no further jobs available
EOF

test_expect_success 'run_command outputs ' '
	test-tool run-command run-command-no-jobs 3 sh -c "printf \"%s\n%s\n\" Hello World" 2>actual &&
	test_cmp expect actual
'

test_trace () {
	expect="$1"
	shift
	GIT_TRACE=1 test-tool run-command "$@" run-command true 2>&1 >/dev/null | \
		sed -e 's/.* run_command: //' -e '/trace: .*/d' \
			-e '/RUNTIME_PREFIX requested/d' >actual &&
	echo "$expect true" >expect &&
	test_cmp expect actual
}

test_expect_success 'GIT_TRACE with environment variables' '
	test_trace "abc=1 def=2" env abc=1 env def=2 &&
	test_trace "abc=2" env abc env abc=1 env abc=2 &&
	test_trace "abc=2" env abc env abc=2 &&
	(
		abc=1 && export abc &&
		test_trace "def=1" env abc=1 env def=1
	) &&
	(
		abc=1 && export abc &&
		test_trace "def=1" env abc env abc=1 env def=1
	) &&
	test_trace "def=1" env non-exist env def=1 &&
	test_trace "abc=2" env abc=1 env abc env abc=2 &&
	(
		abc=1 def=2 && export abc def &&
		test_trace "unset abc def;" env abc env def
	) &&
	(
		abc=1 def=2 && export abc def &&
		test_trace "unset def; abc=3" env abc env def env abc=3
	) &&
	(
		abc=1 && export abc &&
		test_trace "unset abc;" env abc=2 env abc
	)
'

test_expect_success MINGW 'verify curlies are quoted properly' '
	: force the rev-parse through the MSYS2 Bash &&
	git -c alias.r="!git rev-parse" r -- a{b}c >actual &&
	cat >expect <<-\EOF &&
	--
	a{b}c
	EOF
	test_cmp expect actual
'

test_expect_success MINGW 'can spawn with argv[0] containing spaces' '
	cp "$GIT_BUILD_DIR/t/helper/test-fake-ssh$X" ./ &&
	test_must_fail "$PWD/test-fake-ssh$X" 2>err &&
	grep TRASH_DIRECTORY err
'

test_done
