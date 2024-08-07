#!/bin/sh

test_description='rm --pathspec-from-file'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_tick

test_expect_success setup '
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&
	git add fileA.t fileB.t fileC.t fileD.t &&
	git commit -m "files" &&

	git tag checkpoint
'

restore_checkpoint () {
	git reset --hard checkpoint
}

verify_expect () {
	git status --porcelain --untracked-files=no -- fileA.t fileB.t fileC.t fileD.t >actual &&
	test_cmp expect actual
}

test_expect_success 'simplest' '
	restore_checkpoint &&

	cat >expect <<-\EOF &&
	D  fileA.t
	EOF

	echo fileA.t | git rm --pathspec-from-file=- &&
	verify_expect
'

test_expect_success '--pathspec-file-nul' '
	restore_checkpoint &&

	cat >expect <<-\EOF &&
	D  fileA.t
	D  fileB.t
	EOF

	printf "fileA.t\0fileB.t\0" | git rm --pathspec-from-file=- --pathspec-file-nul &&
	verify_expect
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	cat >expect <<-\EOF &&
	D  fileB.t
	D  fileC.t
	EOF

	printf "fileB.t\nfileC.t\n" | git rm --pathspec-from-file=- &&
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo fileA.t >list &&

	test_must_fail git rm --pathspec-from-file=list -- fileA.t 2>err &&
	test_grep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail git rm --pathspec-file-nul 2>err &&
	test_grep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err &&

	>empty_list &&
	test_must_fail git rm --pathspec-from-file=empty_list 2>err &&
	test_grep -e "No pathspec was given. Which files should I remove?" err
'

test_done
