#!/bin/sh

test_description='stash --pathspec-from-file'

. ./test-lib.sh

test_tick

test_expect_success setup '
	>fileA.t &&
	>fileB.t &&
	>fileC.t &&
	>fileD.t &&
	git add fileA.t fileB.t fileC.t fileD.t &&
	git commit -m "Files" &&

	git tag checkpoint
'

restore_checkpoint () {
	git reset --hard checkpoint
}

verify_expect () {
	git stash show --name-status >actual &&
	test_cmp expect actual
}

test_expect_success 'simplest' '
	restore_checkpoint &&

	# More files are written to make sure that git didnt ignore
	# --pathspec-from-file, stashing everything
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&

	cat >expect <<-\EOF &&
	M	fileA.t
	EOF

	echo fileA.t | git stash push --pathspec-from-file=- &&
	verify_expect
'

test_expect_success '--pathspec-file-nul' '
	restore_checkpoint &&

	# More files are written to make sure that git didnt ignore
	# --pathspec-from-file, stashing everything
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&

	cat >expect <<-\EOF &&
	M	fileA.t
	M	fileB.t
	EOF

	printf "fileA.t\0fileB.t\0" | git stash push --pathspec-from-file=- --pathspec-file-nul &&
	verify_expect
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	# More files are written to make sure that git didnt ignore
	# --pathspec-from-file, stashing everything
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&

	cat >expect <<-\EOF &&
	M	fileB.t
	M	fileC.t
	EOF

	printf "fileB.t\nfileC.t\n" | git stash push --pathspec-from-file=- &&
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo A >fileA.t &&
	echo fileA.t >list &&

	test_must_fail git stash push --pathspec-from-file=list --patch 2>err &&
	test_grep -e "options .--pathspec-from-file. and .--patch. cannot be used together" err &&

	test_must_fail git stash push --pathspec-from-file=list -- fileA.t 2>err &&
	test_grep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail git stash push --pathspec-file-nul 2>err &&
	test_grep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err
'

test_done
