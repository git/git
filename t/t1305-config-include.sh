#!/bin/sh

test_description='test config file include directives'
. ./test-lib.sh

test_expect_success 'include file by absolute path' '
	echo "[test]one = 1" >one &&
	echo "[include]path = \"$(pwd)/one\"" >.gitconfig &&
	echo 1 >expect &&
	git config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'include file by relative path' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	echo 1 >expect &&
	git config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'chained relative paths' '
	mkdir subdir &&
	echo "[test]three = 3" >subdir/three &&
	echo "[include]path = three" >subdir/two &&
	echo "[include]path = subdir/two" >.gitconfig &&
	echo 3 >expect &&
	git config test.three >actual &&
	test_cmp expect actual
'

test_expect_success 'include paths get tilde-expansion' '
	echo "[test]one = 1" >one &&
	echo "[include]path = ~/one" >.gitconfig &&
	echo 1 >expect &&
	git config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'include options can still be examined' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	echo one >expect &&
	git config include.path >actual &&
	test_cmp expect actual
'

test_expect_success 'listing includes option and expansion' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	cat >expect <<-\EOF &&
	include.path=one
	test.one=1
	EOF
	git config --list >actual.full &&
	grep -v ^core actual.full >actual &&
	test_cmp expect actual
'

test_expect_success 'single file lookup does not expand includes by default' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	test_must_fail git config -f .gitconfig test.one &&
	test_must_fail git config --global test.one &&
	echo 1 >expect &&
	git config --includes -f .gitconfig test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'single file list does not expand includes by default' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	echo "include.path=one" >expect &&
	git config -f .gitconfig --list >actual &&
	test_cmp expect actual
'

test_expect_success 'writing config file does not expand includes' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	git config test.two 2 &&
	echo 2 >expect &&
	git config --no-includes test.two >actual &&
	test_cmp expect actual &&
	test_must_fail git config --no-includes test.one
'

test_expect_success 'config modification does not affect includes' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.gitconfig &&
	git config test.one 2 &&
	echo 1 >expect &&
	git config -f one test.one >actual &&
	test_cmp expect actual &&
	cat >expect <<-\EOF &&
	1
	2
	EOF
	git config --get-all test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'missing include files are ignored' '
	cat >.gitconfig <<-\EOF &&
	[include]path = foo
	[test]value = yes
	EOF
	echo yes >expect &&
	git config test.value >actual &&
	test_cmp expect actual
'

test_expect_success 'absolute includes from command line work' '
	echo "[test]one = 1" >one &&
	echo 1 >expect &&
	git -c include.path="$PWD/one" config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from command line fail' '
	echo "[test]one = 1" >one &&
	test_must_fail git -c include.path=one config test.one
'

test_expect_success 'include cycles are detected' '
	cat >.gitconfig <<-\EOF &&
	[test]value = gitconfig
	[include]path = cycle
	EOF
	cat >cycle <<-\EOF &&
	[test]value = cycle
	[include]path = .gitconfig
	EOF
	cat >expect <<-\EOF &&
	gitconfig
	cycle
	EOF
	test_must_fail git config --get-all test.value 2>stderr &&
	grep "exceeded maximum include depth" stderr
'

test_done
