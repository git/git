#!/bin/sh

test_description='various Windows-only path tests'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

if test_have_prereq CYGWIN
then
	alias winpwd='cygpath -aw .'
elif test_have_prereq MINGW
then
	alias winpwd=pwd
else
	skip_all='skipping Windows-only path tests'
	test_done
fi

UNCPATH="$(winpwd)"
case "$UNCPATH" in
[A-Z]:*)
	# Use administrative share e.g. \\localhost\C$\but-sdk-64\usr\src\but
	# (we use forward slashes here because MSYS2 and Git accept them, and
	# they are easier on the eyes)
	UNCPATH="//localhost/${UNCPATH%%:*}\$/${UNCPATH#?:}"
	test -d "$UNCPATH" || {
		skip_all='could not access administrative share; skipping'
		test_done
	}
	;;
*)
	skip_all='skipping UNC path tests, cannot determine current path as UNC'
	test_done
	;;
esac

test_expect_success setup '
	test_cummit initial
'

test_expect_success clone '
	but clone "file://$UNCPATH" clone
'

test_expect_success 'clone without file://' '
	but clone "$UNCPATH" clone-without-file
'

test_expect_success 'clone with backslashed path' '
	BACKSLASHED="$(echo "$UNCPATH" | tr / \\\\)" &&
	but clone "$BACKSLASHED" backslashed
'

test_expect_success fetch '
	but init to-fetch &&
	(
		cd to-fetch &&
		but fetch "$UNCPATH" main
	)
'

test_expect_success push '
	(
		cd clone &&
		but checkout -b to-push &&
		test_cummit to-push &&
		but push origin HEAD
	) &&
	rev="$(but -C clone rev-parse --verify refs/heads/to-push)" &&
	test "$rev" = "$(but rev-parse --verify refs/heads/to-push)"
'

test_expect_success MINGW 'remote nick cannot contain backslashes' '
	BACKSLASHED="$(winpwd | tr / \\\\)" &&
	but ls-remote "$BACKSLASHED" 2>err &&
	test_i18ngrep ! "unable to access" err
'

test_expect_success 'unc alternates' '
	tree="$(but rev-parse HEAD:)" &&
	mkdir test-unc-alternate &&
	(
		cd test-unc-alternate &&
		but init &&
		test_must_fail but show $tree &&
		echo "$UNCPATH/.but/objects" >.but/objects/info/alternates &&
		but show $tree
	)
'

test_done
