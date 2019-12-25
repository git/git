#!/bin/sh

test_description='add --pathspec-from-file'

. ./test-lib.sh

test_tick

test_expect_success setup '
	test_commit file0 &&
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t
'

restore_checkpoint () {
	git reset
}

verify_expect () {
	git status --porcelain --untracked-files=no -- fileA.t fileB.t fileC.t fileD.t >actual &&
	test_cmp expect actual
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	echo fileA.t | git add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	echo fileA.t >list &&
	git add --pathspec-from-file=list &&

	cat >expect <<-\EOF &&
	A  fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	printf "fileA.t\0fileB.t\0" | git add --pathspec-from-file=- --pathspec-file-nul &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t\n" | git add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t" | git add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\r\nfileB.t\r\n" | git add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'quotes' '
	restore_checkpoint &&

	printf "\"file\\101.t\"" | git add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	EOF
	verify_expect
'

test_expect_success 'quotes not compatible with --pathspec-file-nul' '
	restore_checkpoint &&

	printf "\"file\\101.t\"" >list &&
	test_must_fail git add --pathspec-from-file=list --pathspec-file-nul
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	printf "fileB.t\nfileC.t\n" | git add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileB.t
	A  fileC.t
	EOF
	verify_expect
'

test_done
