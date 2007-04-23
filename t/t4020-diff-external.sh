#!/bin/sh

test_description='external diff interface test'

. ./test-lib.sh

_z40=0000000000000000000000000000000000000000

test_expect_success setup '

	test_tick &&
	echo initial >file &&
	git add file &&
	git commit -m initial &&

	test_tick &&
	echo second >file &&
	git add file &&
	git commit -m second &&

	test_tick &&
	echo third >file
'

test_expect_success 'GIT_EXTERNAL_DIFF environment' '

	GIT_EXTERNAL_DIFF=echo git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}

'

test_expect_success 'GIT_EXTERNAL_DIFF environment should apply only to diff' '

	GIT_EXTERNAL_DIFF=echo git log -p -1 HEAD |
	grep "^diff --git a/file b/file"

'

test_expect_success 'diff attribute' '

	git config diff.parrot.command echo &&

	echo >.gitattributes "file diff=parrot" &&

	git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}

'

test_expect_success 'diff attribute should apply only to diff' '

	git log -p -1 HEAD |
	grep "^diff --git a/file b/file"

'

test_expect_success 'diff attribute' '

	git config --unset diff.parrot.command &&
	git config diff.color.command echo &&

	echo >.gitattributes "file diff=color" &&

	git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}

'

test_expect_success 'diff attribute should apply only to diff' '

	git log -p -1 HEAD |
	grep "^diff --git a/file b/file"

'

test_done
