#!/bin/sh
#
# Copyright (c) 2009 Ilari Liusvaara
#

test_description='Test run command'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

cat >hello-script <<-EOF
	#!$SHELL_PATH
	cat hello-script
EOF

test_expect_success MINGW 'subprocess inherits only std handles' '
	test-tool run-command inherited-handle
'

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
	test-tool run-command run-command-parallel 5 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>actual &&
	test_must_be_empty out &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with more jobs available than tasks --duplicate-output' '
	test-tool run-command --duplicate-output run-command-parallel 5 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_must_be_empty out &&
	test 4 = $(grep -c "duplicate_output: Hello" err) &&
	test 4 = $(grep -c "duplicate_output: World" err) &&
	sed "/duplicate_output/d" err > err1 &&
	test_cmp expect err1
'

test_expect_success 'run_command runs ungrouped in parallel with more jobs available than tasks' '
	test-tool run-command --ungroup run-command-parallel 5 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_line_count = 8 out &&
	test_line_count = 4 err
'

test_expect_success 'run_command runs in parallel with as many jobs as tasks' '
	test-tool run-command run-command-parallel 4 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>actual &&
	test_must_be_empty out &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with as many jobs as tasks --duplicate-output' '
	test-tool run-command --duplicate-output run-command-parallel 4 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_must_be_empty out &&
	test 4 = $(grep -c "duplicate_output: Hello" err) &&
	test 4 = $(grep -c "duplicate_output: World" err) &&
	sed "/duplicate_output/d" err > err1 &&
	test_cmp expect err1
'

test_expect_success 'run_command runs ungrouped in parallel with as many jobs as tasks' '
	test-tool run-command --ungroup run-command-parallel 4 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_line_count = 8 out &&
	test_line_count = 4 err
'

test_expect_success 'run_command runs in parallel with more tasks than jobs available' '
	test-tool run-command run-command-parallel 3 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>actual &&
	test_must_be_empty out &&
	test_cmp expect actual
'

test_expect_success 'run_command runs in parallel with more tasks than jobs available --duplicate-output' '
	test-tool run-command --duplicate-output run-command-parallel 3 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_must_be_empty out &&
	test 4 = $(grep -c "duplicate_output: Hello" err) &&
	test 4 = $(grep -c "duplicate_output: World" err) &&
	sed "/duplicate_output/d" err > err1 &&
	test_cmp expect err1
'

test_expect_success 'run_command runs ungrouped in parallel with more tasks than jobs available' '
	test-tool run-command --ungroup run-command-parallel 3 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_line_count = 8 out &&
	test_line_count = 4 err
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
	test-tool run-command run-command-abort 3 false >out 2>actual &&
	test_must_be_empty out &&
	test_cmp expect actual
'

test_expect_success 'run_command is asked to abort gracefully --duplicate-output' '
	test-tool run-command --duplicate-output run-command-abort 3 false >out 2>err &&
	test_must_be_empty out &&
	test_cmp expect err
'

test_expect_success 'run_command is asked to abort gracefully (ungroup)' '
	test-tool run-command --ungroup run-command-abort 3 false >out 2>err &&
	test_must_be_empty out &&
	test_line_count = 6 err
'

cat >expect <<-EOF
no further jobs available
EOF

test_expect_success 'run_command outputs ' '
	test-tool run-command run-command-no-jobs 3 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>actual &&
	test_must_be_empty out &&
	test_cmp expect actual
'

test_expect_success 'run_command outputs --duplicate-output' '
	test-tool run-command --duplicate-output run-command-no-jobs 3 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_must_be_empty out &&
	test_cmp expect err
'

test_expect_success 'run_command outputs (ungroup) ' '
	test-tool run-command --ungroup run-command-no-jobs 3 sh -c "printf \"%s\n%s\n\" Hello World" >out 2>err &&
	test_must_be_empty out &&
	test_cmp expect err
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

test_expect_success MINGW 'can spawn .bat with argv[0] containing spaces' '
	bat="$TRASH_DIRECTORY/bat with spaces in name.bat" &&

	# Every .bat invocation will log its arguments to file "out"
	rm -f out &&
	echo "echo %* >>out" >"$bat" &&

	# Ask git to invoke .bat; clone will fail due to fake SSH helper
	test_must_fail env GIT_SSH="$bat" git clone myhost:src ssh-clone &&

	# Spawning .bat can fail if there are two quoted cmd.exe arguments.
	# .bat itself is first (due to spaces in name), so just one more is
	# needed to verify. GIT_SSH will invoke .bat multiple times:
	# 1) -G myhost
	# 2) myhost "git-upload-pack src"
	# First invocation will always succeed. Test the second one.
	grep "git-upload-pack" out
'

test_done
