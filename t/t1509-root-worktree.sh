#!/bin/sh

test_description='Test Git when git repository is located at root

This test requires write access in root. Do not bother if you do not
have a throwaway chroot or VM.

Script t1509/prepare-chroot.sh may help you setup chroot, then you
can chroot in and execute this test from there.
'

. ./test-lib.sh

test_cmp_val() {
	echo "$1" > expected
	echo "$2" > result
	test_cmp expected result
}

test_vars() {
	test_expect_success "$1: gitdir" '
		test_cmp_val "'"$2"'" "$(git rev-parse --git-dir)"
	'

	test_expect_success "$1: worktree" '
		test_cmp_val "'"$3"'" "$(git rev-parse --show-toplevel)"
	'

	test_expect_success "$1: prefix" '
		test_cmp_val "'"$4"'" "$(git rev-parse --show-prefix)"
	'
}

test_foobar_root() {
	test_expect_success 'add relative' '
		test -z "$(cd / && git ls-files)" &&
		git add foo/foome &&
		git add foo/bar/barme &&
		git add me &&
		( cd / && git ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(git rev-parse --git-dir)/index"
	'

	test_expect_success 'add absolute' '
		test -z "$(cd / && git ls-files)" &&
		git add /foo/foome &&
		git add /foo/bar/barme &&
		git add /me &&
		( cd / && git ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(git rev-parse --git-dir)/index"
	'

}

test_foobar_foo() {
	test_expect_success 'add relative' '
		test -z "$(cd / && git ls-files)" &&
		git add foome &&
		git add bar/barme &&
		git add ../me &&
		( cd / && git ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(git rev-parse --git-dir)/index"
	'

	test_expect_success 'add absolute' '
		test -z "$(cd / && git ls-files)" &&
		git add /foo/foome &&
		git add /foo/bar/barme &&
		git add /me &&
		( cd / && git ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(git rev-parse --git-dir)/index"
	'
}

test_foobar_foobar() {
	test_expect_success 'add relative' '
		test -z "$(cd / && git ls-files)" &&
		git add ../foome &&
		git add barme &&
		git add ../../me &&
		( cd / && git ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(git rev-parse --git-dir)/index"
	'

	test_expect_success 'add absolute' '
		test -z "$(cd / && git ls-files)" &&
		git add /foo/foome &&
		git add /foo/bar/barme &&
		git add /me &&
		( cd / && git ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(git rev-parse --git-dir)/index"
	'
}

if ! test_have_prereq POSIXPERM || ! [ -w / ]; then
	skip_all="Dangerous test skipped. Read this test if you want to execute it"
	test_done
fi

if [ "$IKNOWWHATIAMDOING" != "YES" ]; then
	skip_all="You must set env var IKNOWWHATIAMDOING=YES in order to run this test"
	test_done
fi

if [ "$UID" = 0 ]; then
	skip_all="No you can't run this with root"
	test_done
fi

ONE_SHA1=d00491fd7e5bb6fa28c517a0bb32b8b506539d4d

test_expect_success 'setup' '
	rm -rf /foo
	mkdir /foo &&
	mkdir /foo/bar &&
	echo 1 > /foo/foome &&
	echo 1 > /foo/bar/barme &&
	echo 1 > /me
'

say "GIT_DIR absolute, GIT_WORK_TREE set"

test_expect_success 'go to /' 'cd /'

cat >ls.expected <<EOF
100644 $ONE_SHA1 0	foo/bar/barme
100644 $ONE_SHA1 0	foo/foome
100644 $ONE_SHA1 0	me
EOF

export GIT_DIR="$TRASH_DIRECTORY/.git"
export GIT_WORK_TREE=/

test_vars 'abs gitdir, root' "$GIT_DIR" "/" ""
test_foobar_root

test_expect_success 'go to /foo' 'cd /foo'

test_vars 'abs gitdir, foo' "$GIT_DIR" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'

test_vars 'abs gitdir, foo/bar' "$GIT_DIR" "/" "foo/bar/"
test_foobar_foobar

say "GIT_DIR relative, GIT_WORK_TREE set"

test_expect_success 'go to /' 'cd /'

export GIT_DIR="$(echo $TRASH_DIRECTORY|sed 's,^/,,')/.git"
export GIT_WORK_TREE=/

test_vars 'rel gitdir, root' "$GIT_DIR" "/" ""
test_foobar_root

test_expect_success 'go to /foo' 'cd /foo'

export GIT_DIR="../$TRASH_DIRECTORY/.git"
export GIT_WORK_TREE=/

test_vars 'rel gitdir, foo' "$TRASH_DIRECTORY/.git" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'

export GIT_DIR="../../$TRASH_DIRECTORY/.git"
export GIT_WORK_TREE=/

test_vars 'rel gitdir, foo/bar' "$TRASH_DIRECTORY/.git" "/" "foo/bar/"
test_foobar_foobar

say "GIT_DIR relative, GIT_WORK_TREE relative"

test_expect_success 'go to /' 'cd /'

export GIT_DIR="$(echo $TRASH_DIRECTORY|sed 's,^/,,')/.git"
export GIT_WORK_TREE=.

test_vars 'rel gitdir, root' "$GIT_DIR" "/" ""
test_foobar_root

test_expect_success 'go to /' 'cd /foo'

export GIT_DIR="../$TRASH_DIRECTORY/.git"
export GIT_WORK_TREE=..

test_vars 'rel gitdir, foo' "$TRASH_DIRECTORY/.git" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'

export GIT_DIR="../../$TRASH_DIRECTORY/.git"
export GIT_WORK_TREE=../..

test_vars 'rel gitdir, foo/bar' "$TRASH_DIRECTORY/.git" "/" "foo/bar/"
test_foobar_foobar

say ".git at root"

unset GIT_DIR
unset GIT_WORK_TREE

test_expect_success 'go to /' 'cd /'
test_expect_success 'setup' '
	rm -rf /.git
	echo "Initialized empty Git repository in /.git/" > expected &&
	git init > result &&
	test_cmp expected result
'

test_vars 'auto gitdir, root' ".git" "/" ""
test_foobar_root

test_expect_success 'go to /foo' 'cd /foo'
test_vars 'auto gitdir, foo' "/.git" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'
test_vars 'auto gitdir, foo/bar' "/.git" "/" "foo/bar/"
test_foobar_foobar

test_expect_success 'cleanup' 'rm -rf /.git'

say "auto bare gitdir"

# DESTROYYYYY!!!!!
test_expect_success 'setup' '
	rm -rf /refs /objects /info /hooks
	rm /*
	cd / &&
	echo "Initialized empty Git repository in /" > expected &&
	git init --bare > result &&
	test_cmp expected result
'

test_vars 'auto gitdir, root' "." "" ""

test_expect_success 'go to /foo' 'cd /foo'

test_vars 'auto gitdir, root' "/" "" ""

test_done
