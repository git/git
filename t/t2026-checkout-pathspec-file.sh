#!/bin/sh

test_description='checkout --pathspec-from-file'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_tick

test_expect_success setup '
	test_cummit file0 &&

	echo 1 >fileA.t &&
	echo 1 >fileB.t &&
	echo 1 >fileC.t &&
	echo 1 >fileD.t &&
	but add fileA.t fileB.t fileC.t fileD.t &&
	but cummit -m "files 1" &&

	echo 2 >fileA.t &&
	echo 2 >fileB.t &&
	echo 2 >fileC.t &&
	echo 2 >fileD.t &&
	but add fileA.t fileB.t fileC.t fileD.t &&
	but cummit -m "files 2" &&

	but tag checkpoint
'

restore_checkpoint () {
	but reset --hard checkpoint
}

verify_expect () {
	but status --porcelain --untracked-files=no -- fileA.t fileB.t fileC.t fileD.t >actual &&
	test_cmp expect actual
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	echo fileA.t | but checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	echo fileA.t >list &&
	but checkout --pathspec-from-file=list HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	printf "fileA.t\0fileB.t\0" | but checkout --pathspec-from-file=- --pathspec-file-nul HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t\n" | but checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t" | but checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\r\nfileB.t\r\n" | but checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'quotes' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	but checkout --pathspec-from-file=list HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	EOF
	verify_expect
'

test_expect_success 'quotes not compatible with --pathspec-file-nul' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	test_must_fail but checkout --pathspec-from-file=list --pathspec-file-nul HEAD^1
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	printf "fileB.t\nfileC.t\n" | but checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileB.t
	M  fileC.t
	EOF
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo fileA.t >list &&

	test_must_fail but checkout --pathspec-from-file=list --detach 2>err &&
	test_i18ngrep -e "options .--pathspec-from-file. and .--detach. cannot be used together" err &&

	test_must_fail but checkout --pathspec-from-file=list --patch 2>err &&
	test_i18ngrep -e "options .--pathspec-from-file. and .--patch. cannot be used together" err &&

	test_must_fail but checkout --pathspec-from-file=list -- fileA.t 2>err &&
	test_i18ngrep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail but checkout --pathspec-file-nul 2>err &&
	test_i18ngrep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err
'

test_done
