#!/bin/bash

test_description='config-managed multihooks, including git-hook command'

. ./test-lib.sh

<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< HEAD
================================
ROOT=
if test_have_prereq MINGW
then
	# In Git for Windows, Unix-like paths work only in shell scripts;
	# `git.exe`, however, will prefix them with the pseudo root directory
	# (of the Unix shell). Let's accommodate for that.
	ROOT="$(cd / && pwd)"
fi

setup_hooks () {
	test_config hook.pre-commit.command "/path/ghi" --add
	test_config_global hook.pre-commit.command "/path/def" --add
}

setup_hookcmd () {
	test_config hook.pre-commit.command "abc" --add
	test_config_global hookcmd.abc.command "/path/abc" --add
}

>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> upstream/seen
test_expect_success 'git hook rejects commands without a mode' '
	test_must_fail git hook pre-commit
'


test_expect_success 'git hook rejects commands without a hookname' '
	test_must_fail git hook list
'

<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< HEAD
test_expect_success 'setup hooks in global, and local' '
	git config --add --local hook.pre-commit.command "/path/ghi" &&
	git config --add --global hook.pre-commit.command "/path/def"
'

test_expect_success 'git hook list orders by config order' '
	cat >expected <<-\EOF &&
	global:	/path/def
	local:	/path/ghi
================================
test_expect_success 'git hook list orders by config order' '
	setup_hooks &&

	cat >expected <<-EOF &&
	global:	$ROOT/path/def
	local:	$ROOT/path/ghi
>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> upstream/seen
	EOF

	git hook list pre-commit >actual &&
	test_cmp expected actual
'

test_expect_success 'git hook list dereferences a hookcmd' '
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< HEAD
	git config --add --local hook.pre-commit.command "abc" &&
	git config --add --global hookcmd.abc.command "/path/abc" &&

	cat >expected <<-\EOF &&
	global:	/path/def
	local:	/path/ghi
	local:	/path/abc
================================
	setup_hooks &&
	setup_hookcmd &&

	cat >expected <<-EOF &&
	global:	$ROOT/path/def
	local:	$ROOT/path/ghi
	local:	$ROOT/path/abc
>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> upstream/seen
	EOF

	git hook list pre-commit >actual &&
	test_cmp expected actual
'

test_expect_success 'git hook list reorders on duplicate commands' '
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< HEAD
	git config --add --local hook.pre-commit.command "/path/def" &&

	cat >expected <<-\EOF &&
	local:	/path/ghi
	local:	/path/abc
	local:	/path/def
================================
	setup_hooks &&

	test_config hook.pre-commit.command "/path/def" --add &&

	cat >expected <<-EOF &&
	local:	$ROOT/path/ghi
	local:	$ROOT/path/def
>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> upstream/seen
	EOF

	git hook list pre-commit >actual &&
	test_cmp expected actual
'

test_expect_success 'git hook list --porcelain prints just the command' '
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< HEAD
	cat >expected <<-\EOF &&
	/path/ghi
	/path/abc
	/path/def
================================
	setup_hooks &&

	cat >expected <<-EOF &&
	$ROOT/path/def
	$ROOT/path/ghi
>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> upstream/seen
	EOF

	git hook list --porcelain pre-commit >actual &&
	test_cmp expected actual
'

<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< HEAD
================================
test_expect_success 'inline hook definitions execute oneliners' '
	test_config hook.pre-commit.command "echo \"Hello World\"" &&

	echo "Hello World" >expected &&

	# hooks are run with stdout_to_stderr = 1
	git hook run pre-commit 2>actual &&
	test_cmp expected actual
'

test_expect_success 'inline hook definitions resolve paths' '
	cat >~/sample-hook.sh <<-EOF &&
	echo \"Sample Hook\"
	EOF

	test_when_finished "rm ~/sample-hook.sh" &&

	chmod +x ~/sample-hook.sh &&

	test_config hook.pre-commit.command "~/sample-hook.sh" &&

	echo \"Sample Hook\" >expected &&

	# hooks are run with stdout_to_stderr = 1
	git hook run pre-commit 2>actual &&
	test_cmp expected actual
'

>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> upstream/seen
test_done
