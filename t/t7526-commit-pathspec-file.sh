#!/bin/sh

test_description='commit --pathspec-from-file'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_tick

test_expect_success setup '
	test_commit file0 &&
	git tag checkpoint &&

	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&
	git add fileA.t fileB.t fileC.t fileD.t
'

restore_checkpoint () {
	git reset --soft checkpoint
}

verify_expect () {
	git diff-tree --no-commit-id --name-status -r HEAD >actual &&
	test_cmp expect actual
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	echo fileA.t | git commit --pathspec-from-file=- -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	echo fileA.t >list &&
	git commit --pathspec-from-file=list -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	printf "fileA.t\0fileB.t\0" | git commit --pathspec-from-file=- --pathspec-file-nul -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	A	fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t\n" | git commit --pathspec-from-file=- -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	A	fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	printf "fileA.t\nfileB.t" | git commit --pathspec-from-file=- -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	A	fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	printf "fileA.t\r\nfileB.t\r\n" | git commit --pathspec-from-file=- -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	A	fileB.t
	EOF
	verify_expect
'

test_expect_success 'quotes' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	git commit --pathspec-from-file=list -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileA.t
	EOF
	verify_expect expect
'

test_expect_success 'quotes not compatible with --pathspec-file-nul' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	test_must_fail git commit --pathspec-from-file=list --pathspec-file-nul -m "Commit"
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	printf "fileB.t\nfileC.t\n" | git commit --pathspec-from-file=- -m "Commit" &&

	cat >expect <<-\EOF &&
	A	fileB.t
	A	fileC.t
	EOF
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo fileA.t >list &&
	>empty_list &&

	test_must_fail git commit --pathspec-from-file=list --interactive -m "Commit" 2>err &&
	test_grep -e "options .--pathspec-from-file. and .--interactive/--patch. cannot be used together" err &&

	test_must_fail git commit --pathspec-from-file=list --patch -m "Commit" 2>err &&
	test_grep -e "options .--pathspec-from-file. and .--interactive/--patch. cannot be used together" err &&

	test_must_fail git commit --pathspec-from-file=list --all -m "Commit" 2>err &&
	test_grep -e "options .--pathspec-from-file. and .-a. cannot be used together" err &&

	test_must_fail git commit --pathspec-from-file=list -m "Commit" -- fileA.t 2>err &&
	test_grep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail git commit --pathspec-file-nul -m "Commit" 2>err &&
	test_grep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err &&

	test_must_fail git commit --pathspec-from-file=empty_list --include -m "Commit" 2>err &&
	test_grep -e "No paths with --include/--only does not make sense." err &&

	test_must_fail git commit --pathspec-from-file=empty_list --only -m "Commit" 2>err &&
	test_grep -e "No paths with --include/--only does not make sense." err
'

test_done
