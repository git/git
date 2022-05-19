#!/bin/sh

test_description='add --pathspec-from-file'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_tick

test_expect_success setup '
	test_cummit file0 &&
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t
'

restore_checkpoint () {
	but reset
}

verify_expect () {
	but status --porcelain --untracked-files=no -- fileA.t fileB.t fileC.t fileD.t >actual &&
	test_cmp expect actual
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	echo fileA.t | but add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	echo fileA.t >list &&
	but add --pathspec-from-file=list &&

	cat >expect <<-\EOF &&
	A  fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	printf "fileA.t\0fileB.t\0" | but add --pathspec-from-file=- --pathspec-file-nul &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t\n" | but add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t" | but add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\r\nfileB.t\r\n" | but add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileA.t
	A  fileB.t
	EOF
	verify_expect
'

test_expect_success 'quotes' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	but add --pathspec-from-file=list &&

	cat >expect <<-\EOF &&
	A  fileA.t
	EOF
	verify_expect
'

test_expect_success 'quotes not compatible with --pathspec-file-nul' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	test_must_fail but add --pathspec-from-file=list --pathspec-file-nul
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	printf "fileB.t\nfileC.t\n" | but add --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	A  fileB.t
	A  fileC.t
	EOF
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo fileA.t >list &&
	>empty_list &&

	test_must_fail but add --pathspec-from-file=list --interactive 2>err &&
	test_i18ngrep -e "options .--pathspec-from-file. and .--interactive/--patch. cannot be used together" err &&

	test_must_fail but add --pathspec-from-file=list --patch 2>err &&
	test_i18ngrep -e "options .--pathspec-from-file. and .--interactive/--patch. cannot be used together" err &&

	test_must_fail but add --pathspec-from-file=list --edit 2>err &&
	test_i18ngrep -e "options .--pathspec-from-file. and .--edit. cannot be used together" err &&

	test_must_fail but add --pathspec-from-file=list -- fileA.t 2>err &&
	test_i18ngrep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail but add --pathspec-file-nul 2>err &&
	test_i18ngrep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err &&

	# This case succeeds, but still prints to stderr
	but add --pathspec-from-file=empty_list 2>err &&
	test_i18ngrep -e "Nothing specified, nothing added." err
'

test_done
