#!/bin/sh

test_description='Test Git when but repository is located at root

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
	test_expect_success "$1: butdir" '
		test_cmp_val "'"$2"'" "$(but rev-parse --but-dir)"
	'

	test_expect_success "$1: worktree" '
		test_cmp_val "'"$3"'" "$(but rev-parse --show-toplevel)"
	'

	test_expect_success "$1: prefix" '
		test_cmp_val "'"$4"'" "$(but rev-parse --show-prefix)"
	'
}

test_foobar_root() {
	test_expect_success 'add relative' '
		test -z "$(cd / && but ls-files)" &&
		but add foo/foome &&
		but add foo/bar/barme &&
		but add me &&
		( cd / && but ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(but rev-parse --but-dir)/index"
	'

	test_expect_success 'add absolute' '
		test -z "$(cd / && but ls-files)" &&
		but add /foo/foome &&
		but add /foo/bar/barme &&
		but add /me &&
		( cd / && but ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(but rev-parse --but-dir)/index"
	'

}

test_foobar_foo() {
	test_expect_success 'add relative' '
		test -z "$(cd / && but ls-files)" &&
		but add foome &&
		but add bar/barme &&
		but add ../me &&
		( cd / && but ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(but rev-parse --but-dir)/index"
	'

	test_expect_success 'add absolute' '
		test -z "$(cd / && but ls-files)" &&
		but add /foo/foome &&
		but add /foo/bar/barme &&
		but add /me &&
		( cd / && but ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(but rev-parse --but-dir)/index"
	'
}

test_foobar_foobar() {
	test_expect_success 'add relative' '
		test -z "$(cd / && but ls-files)" &&
		but add ../foome &&
		but add barme &&
		but add ../../me &&
		( cd / && but ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(but rev-parse --but-dir)/index"
	'

	test_expect_success 'add absolute' '
		test -z "$(cd / && but ls-files)" &&
		but add /foo/foome &&
		but add /foo/bar/barme &&
		but add /me &&
		( cd / && but ls-files --stage ) > result &&
		test_cmp /ls.expected result &&
		rm "$(but rev-parse --but-dir)/index"
	'
}

if ! test -w /
then
	skip_all="Test requiring writable / skipped. Read this test if you want to run it"
	test_done
fi

if  test -e /refs || test -e /objects || test -e /info || test -e /hooks ||
    test -e /.but || test -e /foo || test -e /me
then
	skip_all="Skip test that clobbers existing files in /"
	test_done
fi

if [ "$IKNOWWHATIAMDOING" != "YES" ]; then
	skip_all="You must set env var IKNOWWHATIAMDOING=YES in order to run this test"
	test_done
fi

if ! test_have_prereq NOT_ROOT
then
	skip_all="No you can't run this as root"
	test_done
fi

ONE_SHA1=d00491fd7e5bb6fa28c517a0bb32b8b506539d4d

test_expect_success 'setup' '
	rm -rf /foo &&
	mkdir /foo &&
	mkdir /foo/bar &&
	echo 1 > /foo/foome &&
	echo 1 > /foo/bar/barme &&
	echo 1 > /me
'

say "BUT_DIR absolute, BUT_WORK_TREE set"

test_expect_success 'go to /' 'cd /'

cat >ls.expected <<EOF
100644 $ONE_SHA1 0	foo/bar/barme
100644 $ONE_SHA1 0	foo/foome
100644 $ONE_SHA1 0	me
EOF

BUT_DIR="$TRASH_DIRECTORY/.but" && export BUT_DIR
BUT_WORK_TREE=/ && export BUT_WORK_TREE

test_vars 'abs butdir, root' "$BUT_DIR" "/" ""
test_foobar_root

test_expect_success 'go to /foo' 'cd /foo'

test_vars 'abs butdir, foo' "$BUT_DIR" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'

test_vars 'abs butdir, foo/bar' "$BUT_DIR" "/" "foo/bar/"
test_foobar_foobar

say "BUT_DIR relative, BUT_WORK_TREE set"

test_expect_success 'go to /' 'cd /'

BUT_DIR="$(echo $TRASH_DIRECTORY|sed 's,^/,,')/.but" && export BUT_DIR
BUT_WORK_TREE=/ && export BUT_WORK_TREE

test_vars 'rel butdir, root' "$BUT_DIR" "/" ""
test_foobar_root

test_expect_success 'go to /foo' 'cd /foo'

BUT_DIR="../$TRASH_DIRECTORY/.but" && export BUT_DIR
BUT_WORK_TREE=/ && export BUT_WORK_TREE

test_vars 'rel butdir, foo' "$TRASH_DIRECTORY/.but" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'

BUT_DIR="../../$TRASH_DIRECTORY/.but" && export BUT_DIR
BUT_WORK_TREE=/ && export BUT_WORK_TREE

test_vars 'rel butdir, foo/bar' "$TRASH_DIRECTORY/.but" "/" "foo/bar/"
test_foobar_foobar

say "BUT_DIR relative, BUT_WORK_TREE relative"

test_expect_success 'go to /' 'cd /'

BUT_DIR="$(echo $TRASH_DIRECTORY|sed 's,^/,,')/.but" && export BUT_DIR
BUT_WORK_TREE=. && export BUT_WORK_TREE

test_vars 'rel butdir, root' "$BUT_DIR" "/" ""
test_foobar_root

test_expect_success 'go to /' 'cd /foo'

BUT_DIR="../$TRASH_DIRECTORY/.but" && export BUT_DIR
BUT_WORK_TREE=.. && export BUT_WORK_TREE

test_vars 'rel butdir, foo' "$TRASH_DIRECTORY/.but" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'

BUT_DIR="../../$TRASH_DIRECTORY/.but" && export BUT_DIR
BUT_WORK_TREE=../.. && export BUT_WORK_TREE

test_vars 'rel butdir, foo/bar' "$TRASH_DIRECTORY/.but" "/" "foo/bar/"
test_foobar_foobar

say ".but at root"

unset BUT_DIR
unset BUT_WORK_TREE

test_expect_success 'go to /' 'cd /'
test_expect_success 'setup' '
	rm -rf /.but &&
	echo "Initialized empty Git repository in /.but/" > expected &&
	but init > result &&
	test_cmp expected result
'

test_vars 'auto butdir, root' ".but" "/" ""
test_foobar_root

test_expect_success 'go to /foo' 'cd /foo'
test_vars 'auto butdir, foo' "/.but" "/" "foo/"
test_foobar_foo

test_expect_success 'go to /foo/bar' 'cd /foo/bar'
test_vars 'auto butdir, foo/bar' "/.but" "/" "foo/bar/"
test_foobar_foobar

test_expect_success 'cleanup' 'rm -rf /.but'

say "auto bare butdir"

# DESTROYYYYY!!!!!
test_expect_success 'setup' '
	rm -rf /refs /objects /info /hooks &&
	rm -f /expected /ls.expected /me /result &&
	cd / &&
	echo "Initialized empty Git repository in /" > expected &&
	but init --bare > result &&
	test_cmp expected result
'

test_vars 'auto butdir, root' "." "" ""

test_expect_success 'go to /foo' 'cd /foo'

test_vars 'auto butdir, root' "/" "" ""

test_done
