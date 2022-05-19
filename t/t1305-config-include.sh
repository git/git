#!/bin/sh

test_description='test config file include directives'
TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Force setup_explicit_but_dir() to run until the end. This is needed
# by some tests to make sure real_path() is called on $BUT_DIR. The
# caller needs to make sure but commands are run from a subdirectory
# though or real_path() will not be called.
force_setup_explicit_but_dir() {
    BUT_DIR="$(pwd)/.but"
    BUT_WORK_TREE="$(pwd)"
    export BUT_DIR BUT_WORK_TREE
}

test_expect_success 'include file by absolute path' '
	echo "[test]one = 1" >one &&
	echo "[include]path = \"$(pwd)/one\"" >.butconfig &&
	echo 1 >expect &&
	but config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'include file by relative path' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	echo 1 >expect &&
	but config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'chained relative paths' '
	mkdir subdir &&
	echo "[test]three = 3" >subdir/three &&
	echo "[include]path = three" >subdir/two &&
	echo "[include]path = subdir/two" >.butconfig &&
	echo 3 >expect &&
	but config test.three >actual &&
	test_cmp expect actual
'

test_expect_success 'include paths get tilde-expansion' '
	echo "[test]one = 1" >one &&
	echo "[include]path = ~/one" >.butconfig &&
	echo 1 >expect &&
	but config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'include options can still be examined' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	echo one >expect &&
	but config include.path >actual &&
	test_cmp expect actual
'

test_expect_success 'listing includes option and expansion' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	cat >expect <<-\EOF &&
	include.path=one
	test.one=1
	EOF
	but config --list >actual.full &&
	grep -v -e ^core -e ^extensions actual.full >actual &&
	test_cmp expect actual
'

test_expect_success 'single file lookup does not expand includes by default' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	test_must_fail but config -f .butconfig test.one &&
	test_must_fail but config --global test.one &&
	echo 1 >expect &&
	but config --includes -f .butconfig test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'single file list does not expand includes by default' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	echo "include.path=one" >expect &&
	but config -f .butconfig --list >actual &&
	test_cmp expect actual
'

test_expect_success 'writing config file does not expand includes' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	but config test.two 2 &&
	echo 2 >expect &&
	but config --no-includes test.two >actual &&
	test_cmp expect actual &&
	test_must_fail but config --no-includes test.one
'

test_expect_success 'config modification does not affect includes' '
	echo "[test]one = 1" >one &&
	echo "[include]path = one" >.butconfig &&
	but config test.one 2 &&
	echo 1 >expect &&
	but config -f one test.one >actual &&
	test_cmp expect actual &&
	cat >expect <<-\EOF &&
	1
	2
	EOF
	but config --get-all test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'missing include files are ignored' '
	cat >.butconfig <<-\EOF &&
	[include]path = non-existent
	[test]value = yes
	EOF
	echo yes >expect &&
	but config test.value >actual &&
	test_cmp expect actual
'

test_expect_success 'absolute includes from command line work' '
	echo "[test]one = 1" >one &&
	echo 1 >expect &&
	but -c include.path="$(pwd)/one" config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from command line fail' '
	echo "[test]one = 1" >one &&
	test_must_fail but -c include.path=one config test.one
'

test_expect_success 'absolute includes from blobs work' '
	echo "[test]one = 1" >one &&
	echo "[include]path=$(pwd)/one" >blob &&
	blob=$(but hash-object -w blob) &&
	echo 1 >expect &&
	but config --blob=$blob test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from blobs fail' '
	echo "[test]one = 1" >one &&
	echo "[include]path=one" >blob &&
	blob=$(but hash-object -w blob) &&
	test_must_fail but config --blob=$blob test.one
'

test_expect_success 'absolute includes from stdin work' '
	echo "[test]one = 1" >one &&
	echo 1 >expect &&
	echo "[include]path=\"$(pwd)/one\"" |
	but config --file - test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from stdin line fail' '
	echo "[test]one = 1" >one &&
	echo "[include]path=one" |
	test_must_fail but config --file - test.one
'

test_expect_success 'conditional include, both unanchored' '
	but init foo &&
	(
		cd foo &&
		echo "[includeIf \"butdir:foo/\"]path=bar" >>.but/config &&
		echo "[test]one=1" >.but/bar &&
		echo 1 >expect &&
		but config test.one >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, $HOME expansion' '
	(
		cd foo &&
		echo "[includeIf \"butdir:~/foo/\"]path=bar2" >>.but/config &&
		echo "[test]two=2" >.but/bar2 &&
		echo 2 >expect &&
		but config test.two >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, full pattern' '
	(
		cd foo &&
		echo "[includeIf \"butdir:**/foo/**\"]path=bar3" >>.but/config &&
		echo "[test]three=3" >.but/bar3 &&
		echo 3 >expect &&
		but config test.three >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, relative path' '
	echo "[includeIf \"butdir:./foo/.but\"]path=bar4" >>.butconfig &&
	echo "[test]four=4" >bar4 &&
	(
		cd foo &&
		echo 4 >expect &&
		but config test.four >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, both unanchored, icase' '
	(
		cd foo &&
		echo "[includeIf \"butdir/i:FOO/\"]path=bar5" >>.but/config &&
		echo "[test]five=5" >.but/bar5 &&
		echo 5 >expect &&
		but config test.five >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, early config reading' '
	(
		cd foo &&
		echo "[includeIf \"butdir:foo/\"]path=bar6" >>.but/config &&
		echo "[test]six=6" >.but/bar6 &&
		echo 6 >expect &&
		test-tool config read_early_config test.six >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include with /**/' '
	REPO=foo/bar/repo &&
	but init $REPO &&
	cat >>$REPO/.but/config <<-\EOF &&
	[includeIf "butdir:**/foo/**/bar/**"]
	path=bar7
	EOF
	echo "[test]seven=7" >$REPO/.but/bar7 &&
	echo 7 >expect &&
	but -C $REPO config test.seven >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'conditional include, set up symlinked $HOME' '
	mkdir real-home &&
	ln -s real-home home &&
	(
		HOME="$TRASH_DIRECTORY/home" &&
		export HOME &&
		cd "$HOME" &&

		but init foo &&
		cd foo &&
		mkdir sub
	)
'

test_expect_success SYMLINKS 'conditional include, $HOME expansion with symlinks' '
	(
		HOME="$TRASH_DIRECTORY/home" &&
		export HOME &&
		cd "$HOME"/foo &&

		echo "[includeIf \"butdir:~/foo/\"]path=bar2" >>.but/config &&
		echo "[test]two=2" >.but/bar2 &&
		echo 2 >expect &&
		force_setup_explicit_but_dir &&
		but -C sub config test.two >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'conditional include, relative path with symlinks' '
	echo "[includeIf \"butdir:./foo/.but\"]path=bar4" >home/.butconfig &&
	echo "[test]four=4" >home/bar4 &&
	(
		HOME="$TRASH_DIRECTORY/home" &&
		export HOME &&
		cd "$HOME"/foo &&

		echo 4 >expect &&
		force_setup_explicit_but_dir &&
		but -C sub config test.four >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'conditional include, butdir matching symlink' '
	ln -s foo bar &&
	(
		cd bar &&
		echo "[includeIf \"butdir:bar/\"]path=bar7" >>.but/config &&
		echo "[test]seven=7" >.but/bar7 &&
		echo 7 >expect &&
		but config test.seven >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'conditional include, butdir matching symlink, icase' '
	(
		cd bar &&
		echo "[includeIf \"butdir/i:BAR/\"]path=bar8" >>.but/config &&
		echo "[test]eight=8" >.but/bar8 &&
		echo 8 >expect &&
		but config test.eight >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, onbranch' '
	echo "[includeIf \"onbranch:foo-branch\"]path=bar9" >>.but/config &&
	echo "[test]nine=9" >.but/bar9 &&
	but checkout -b main &&
	test_must_fail but config test.nine &&
	but checkout -b foo-branch &&
	echo 9 >expect &&
	but config test.nine >actual &&
	test_cmp expect actual
'

test_expect_success 'conditional include, onbranch, wildcard' '
	echo "[includeIf \"onbranch:?oo-*/**\"]path=bar10" >>.but/config &&
	echo "[test]ten=10" >.but/bar10 &&
	but checkout -b not-foo-branch/a &&
	test_must_fail but config test.ten &&

	echo 10 >expect &&
	but checkout -b foo-branch/a/b/c &&
	but config test.ten >actual &&
	test_cmp expect actual &&

	but checkout -b moo-bar/a &&
	but config test.ten >actual &&
	test_cmp expect actual
'

test_expect_success 'conditional include, onbranch, implicit /** for /' '
	echo "[includeIf \"onbranch:foo-dir/\"]path=bar11" >>.but/config &&
	echo "[test]eleven=11" >.but/bar11 &&
	but checkout -b not-foo-dir/a &&
	test_must_fail but config test.eleven &&

	echo 11 >expect &&
	but checkout -b foo-dir/a/b/c &&
	but config test.eleven >actual &&
	test_cmp expect actual
'

test_expect_success 'include cycles are detected' '
	but init --bare cycle &&
	but -C cycle config include.path cycle &&
	but config -f cycle/cycle include.path config &&
	test_must_fail but -C cycle config --get-all test.value 2>stderr &&
	grep "exceeded maximum include depth" stderr
'

test_done
