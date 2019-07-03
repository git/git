#!/bin/sh

test_description='test config file include directives'
. ./test-lib.sh

# Force setup_explicit_git_dir() to run until the end. This is needed
# by some tests to make sure real_path() is called on $GIT_DIR. The
# caller needs to make sure git commands are run from a subdirectory
# though or real_path() will not be called.
force_setup_explicit_git_dir() {
    GIT_DIR="$(pwd)/.git"
    GIT_WORK_TREE="$(pwd)"
    export GIT_DIR GIT_WORK_TREE
}

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
	[include]path = non-existent
	[test]value = yes
	EOF
	echo yes >expect &&
	git config test.value >actual &&
	test_cmp expect actual
'

test_expect_success 'absolute includes from command line work' '
	echo "[test]one = 1" >one &&
	echo 1 >expect &&
	git -c include.path="$(pwd)/one" config test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from command line fail' '
	echo "[test]one = 1" >one &&
	test_must_fail git -c include.path=one config test.one
'

test_expect_success 'absolute includes from blobs work' '
	echo "[test]one = 1" >one &&
	echo "[include]path=$(pwd)/one" >blob &&
	blob=$(git hash-object -w blob) &&
	echo 1 >expect &&
	git config --blob=$blob test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from blobs fail' '
	echo "[test]one = 1" >one &&
	echo "[include]path=one" >blob &&
	blob=$(git hash-object -w blob) &&
	test_must_fail git config --blob=$blob test.one
'

test_expect_success 'absolute includes from stdin work' '
	echo "[test]one = 1" >one &&
	echo 1 >expect &&
	echo "[include]path=\"$(pwd)/one\"" |
	git config --file - test.one >actual &&
	test_cmp expect actual
'

test_expect_success 'relative includes from stdin line fail' '
	echo "[test]one = 1" >one &&
	echo "[include]path=one" |
	test_must_fail git config --file - test.one
'

test_expect_success 'conditional include, both unanchored' '
	git init foo &&
	(
		cd foo &&
		echo "[includeIf \"gitdir:foo/\"]path=bar" >>.git/config &&
		echo "[test]one=1" >.git/bar &&
		echo 1 >expect &&
		git config test.one >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, $HOME expansion' '
	(
		cd foo &&
		echo "[includeIf \"gitdir:~/foo/\"]path=bar2" >>.git/config &&
		echo "[test]two=2" >.git/bar2 &&
		echo 2 >expect &&
		git config test.two >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, full pattern' '
	(
		cd foo &&
		echo "[includeIf \"gitdir:**/foo/**\"]path=bar3" >>.git/config &&
		echo "[test]three=3" >.git/bar3 &&
		echo 3 >expect &&
		git config test.three >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, relative path' '
	echo "[includeIf \"gitdir:./foo/.git\"]path=bar4" >>.gitconfig &&
	echo "[test]four=4" >bar4 &&
	(
		cd foo &&
		echo 4 >expect &&
		git config test.four >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, both unanchored, icase' '
	(
		cd foo &&
		echo "[includeIf \"gitdir/i:FOO/\"]path=bar5" >>.git/config &&
		echo "[test]five=5" >.git/bar5 &&
		echo 5 >expect &&
		git config test.five >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, early config reading' '
	(
		cd foo &&
		echo "[includeIf \"gitdir:foo/\"]path=bar6" >>.git/config &&
		echo "[test]six=6" >.git/bar6 &&
		echo 6 >expect &&
		test-tool config read_early_config test.six >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include with /**/' '
	REPO=foo/bar/repo &&
	git init $REPO &&
	cat >>$REPO/.git/config <<-\EOF &&
	[includeIf "gitdir:**/foo/**/bar/**"]
	path=bar7
	EOF
	echo "[test]seven=7" >$REPO/.git/bar7 &&
	echo 7 >expect &&
	git -C $REPO config test.seven >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'conditional include, set up symlinked $HOME' '
	mkdir real-home &&
	ln -s real-home home &&
	(
		HOME="$TRASH_DIRECTORY/home" &&
		export HOME &&
		cd "$HOME" &&

		git init foo &&
		cd foo &&
		mkdir sub
	)
'

test_expect_success SYMLINKS 'conditional include, $HOME expansion with symlinks' '
	(
		HOME="$TRASH_DIRECTORY/home" &&
		export HOME &&
		cd "$HOME"/foo &&

		echo "[includeIf \"gitdir:~/foo/\"]path=bar2" >>.git/config &&
		echo "[test]two=2" >.git/bar2 &&
		echo 2 >expect &&
		force_setup_explicit_git_dir &&
		git -C sub config test.two >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'conditional include, relative path with symlinks' '
	echo "[includeIf \"gitdir:./foo/.git\"]path=bar4" >home/.gitconfig &&
	echo "[test]four=4" >home/bar4 &&
	(
		HOME="$TRASH_DIRECTORY/home" &&
		export HOME &&
		cd "$HOME"/foo &&

		echo 4 >expect &&
		force_setup_explicit_git_dir &&
		git -C sub config test.four >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'conditional include, gitdir matching symlink' '
	ln -s foo bar &&
	(
		cd bar &&
		echo "[includeIf \"gitdir:bar/\"]path=bar7" >>.git/config &&
		echo "[test]seven=7" >.git/bar7 &&
		echo 7 >expect &&
		git config test.seven >actual &&
		test_cmp expect actual
	)
'

test_expect_success SYMLINKS 'conditional include, gitdir matching symlink, icase' '
	(
		cd bar &&
		echo "[includeIf \"gitdir/i:BAR/\"]path=bar8" >>.git/config &&
		echo "[test]eight=8" >.git/bar8 &&
		echo 8 >expect &&
		git config test.eight >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'conditional include, onbranch' '
	echo "[includeIf \"onbranch:foo-branch\"]path=bar9" >>.git/config &&
	echo "[test]nine=9" >.git/bar9 &&
	git checkout -b master &&
	test_must_fail git config test.nine &&
	git checkout -b foo-branch &&
	echo 9 >expect &&
	git config test.nine >actual &&
	test_cmp expect actual
'

test_expect_success 'conditional include, onbranch, wildcard' '
	echo "[includeIf \"onbranch:?oo-*/**\"]path=bar10" >>.git/config &&
	echo "[test]ten=10" >.git/bar10 &&
	git checkout -b not-foo-branch/a &&
	test_must_fail git config test.ten &&

	echo 10 >expect &&
	git checkout -b foo-branch/a/b/c &&
	git config test.ten >actual &&
	test_cmp expect actual &&

	git checkout -b moo-bar/a &&
	git config test.ten >actual &&
	test_cmp expect actual
'

test_expect_success 'conditional include, onbranch, implicit /** for /' '
	echo "[includeIf \"onbranch:foo-dir/\"]path=bar11" >>.git/config &&
	echo "[test]eleven=11" >.git/bar11 &&
	git checkout -b not-foo-dir/a &&
	test_must_fail git config test.eleven &&

	echo 11 >expect &&
	git checkout -b foo-dir/a/b/c &&
	git config test.eleven >actual &&
	test_cmp expect actual
'

test_expect_success 'include cycles are detected' '
	git init --bare cycle &&
	git -C cycle config include.path cycle &&
	git config -f cycle/cycle include.path config &&
	test_must_fail \
		env GIT_TEST_GETTEXT_POISON=false \
		git -C cycle config --get-all test.value 2>stderr &&
	grep "exceeded maximum include depth" stderr
'

test_done
