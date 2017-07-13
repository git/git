#!/bin/sh

test_description='various Windows-only path tests'
. ./test-lib.sh

if ! test_have_prereq MINGW; then
	skip_all='skipping Windows-only path tests'
	test_done
fi

UNCPATH="$(pwd)"
case "$UNCPATH" in
[A-Z]:*)
	WITHOUTDRIVE="${UNCPATH#?:}"
	# Use administrative share e.g. \\localhost\C$\git-sdk-64\usr\src\git
	# (we use forward slashes here because MSYS2 and Git accept them, and
	# they are easier on the eyes)
	UNCPATH="//localhost/${UNCPATH%%:*}\$$WITHOUTDRIVE"
	;;
*)
	skip_all='skipping UNC path tests, cannot determine current path as UNC'
	test_done
	;;
esac

test_expect_success 'clone into absolute path lacking a drive prefix' '
	USINGBACKSLASHES="$(echo "$WITHOUTDRIVE"/without-drive-prefix |
		tr / \\\\)" &&
	git clone . "$USINGBACKSLASHES" &&
	test -f without-drive-prefix/.git/HEAD
'

test -d "$UNCPATH" || {
	skip_all='could not access administrative share; skipping'
	test_done
}

test_expect_success setup '
	test_commit initial
'

test_expect_success clone '
	git clone "file://$UNCPATH" clone
'

test_expect_success 'clone without file://' '
	git clone "$UNCPATH" clone-without-file
'

test_expect_success 'clone with backslashed path' '
	BACKSLASHED="$(echo "$UNCPATH" | tr / \\\\)" &&
	git clone "$BACKSLASHED" backslashed
'

test_expect_success fetch '
	git init to-fetch &&
	(
		cd to-fetch &&
		git fetch "$UNCPATH" master
	)
'

test_expect_success push '
	(
		cd clone &&
		git checkout -b to-push &&
		test_commit to-push &&
		git push origin HEAD
	) &&
	rev="$(git -C clone rev-parse --verify refs/heads/to-push)" &&
	test "$rev" = "$(git rev-parse --verify refs/heads/to-push)"
'

test_expect_success 'remote nick cannot contain backslashes' '
	BACKSLASHED="$(pwd | tr / \\\\)" &&
	git ls-remote "$BACKSLASHED" >out 2>err &&
	test_i18ngrep ! "unable to access" err
'

test_expect_success 'unc alternates' '
	tree="$(git rev-parse HEAD:)" &&
	mkdir test-unc-alternate &&
	(
		cd test-unc-alternate &&
		git init &&
		test_must_fail git show $tree &&
		echo "$UNCPATH/.git/objects" >.git/objects/info/alternates &&
		git show $tree
	)
'

test_done
