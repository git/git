#!/bin/sh

test_description='checkout --pathspec-from-file'

. ./test-lib.sh

test_tick

test_expect_success setup '
	test_commit file0 &&

	echo 1 >fileA.t &&
	echo 1 >fileB.t &&
	echo 1 >fileC.t &&
	echo 1 >fileD.t &&
	git add fileA.t fileB.t fileC.t fileD.t &&
	git commit -m "files 1" &&

	echo 2 >fileA.t &&
	echo 2 >fileB.t &&
	echo 2 >fileC.t &&
	echo 2 >fileD.t &&
	git add fileA.t fileB.t fileC.t fileD.t &&
	git commit -m "files 2" &&

	git tag checkpoint
'

restore_checkpoint () {
	git reset --hard checkpoint
}

verify_expect () {
	git status --porcelain --untracked-files=no -- fileA.t fileB.t fileC.t fileD.t >actual &&
	test_cmp expect actual
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	echo fileA.t | git checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	echo fileA.t >list &&
	git checkout --pathspec-from-file=list HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	printf "fileA.t\0fileB.t\0" | git checkout --pathspec-from-file=- --pathspec-file-nul HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t\n" | git checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t" | git checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileA.t
	M  fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\r\nfileB.t\r\n" | git checkout --pathspec-from-file=- HEAD^1 &&

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

	git checkout --pathspec-from-file=list HEAD^1 &&

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

	test_must_fail git checkout --pathspec-from-file=list --pathspec-file-nul HEAD^1
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	printf "fileB.t\nfileC.t\n" | git checkout --pathspec-from-file=- HEAD^1 &&

	cat >expect <<-\EOF &&
	M  fileB.t
	M  fileC.t
	EOF
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo fileA.t >list &&

	test_must_fail git checkout --pathspec-from-file=list --detach 2>err &&
	test_grep -e "options .--pathspec-from-file. and .--detach. cannot be used together" err &&

	test_must_fail git checkout --pathspec-from-file=list --patch 2>err &&
	test_grep -e "options .--pathspec-from-file. and .--patch. cannot be used together" err &&

	test_must_fail git checkout --pathspec-from-file=list -- fileA.t 2>err &&
	test_grep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail git checkout --pathspec-file-nul 2>err &&
	test_grep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err
'

test_done
