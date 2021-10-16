#!/bin/sh

test_description='reset --pathspec-from-file'

. ./test-lib.sh

test_tick

test_expect_success setup '
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&
	git add . &&
	git commit --include . -m "Commit" &&
	git tag checkpoint
'

restore_checkpoint () {
	git reset --hard checkpoint
}

verify_expect () {
	git status --porcelain -- fileA.t fileB.t fileC.t fileD.t >actual &&
	if test "x$1" = 'x!'
	then
		! test_cmp expect actual
	else
		test_cmp expect actual
	fi
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	git rm fileA.t &&
	echo fileA.t | git reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	git rm fileA.t &&
	echo fileA.t >list &&
	git reset --pathspec-from-file=list &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	git rm fileA.t fileB.t &&
	printf "fileA.t\0fileB.t\0" | git reset --pathspec-from-file=- --pathspec-file-nul &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	git rm fileA.t fileB.t &&
	printf "fileA.t\nfileB.t\n" | git reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	git rm fileA.t fileB.t &&
	printf "fileA.t\nfileB.t" | git reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	git rm fileA.t fileB.t &&
	printf "fileA.t\r\nfileB.t\r\n" | git reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'quotes' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	git rm fileA.t &&
	git reset --pathspec-from-file=list &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect
'

test_expect_success 'quotes not compatible with --pathspec-file-nul' '
	restore_checkpoint &&

	cat >list <<-\EOF &&
	"file\101.t"
	EOF

	# Note: "git reset" has not yet learned to fail on wrong pathspecs
	git reset --pathspec-from-file=list --pathspec-file-nul &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect !
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	git rm fileA.t fileB.t fileC.t fileD.t &&
	printf "fileB.t\nfileC.t\n" | git reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	D  fileA.t
	 D fileB.t
	 D fileC.t
	D  fileD.t
	EOF
	verify_expect
'

test_expect_success 'error conditions' '
	restore_checkpoint &&
	echo fileA.t >list &&
	git rm fileA.t &&

	test_must_fail git reset --pathspec-from-file=list --patch 2>err &&
	test_i18ngrep -e "--pathspec-from-file is incompatible with --patch" err &&

	test_must_fail git reset --pathspec-from-file=list -- fileA.t 2>err &&
	test_i18ngrep -e "--pathspec-from-file is incompatible with pathspec arguments" err &&

	test_must_fail git reset --pathspec-file-nul 2>err &&
	test_i18ngrep -e "--pathspec-file-nul requires --pathspec-from-file" err &&

	test_must_fail git reset --soft --pathspec-from-file=list 2>err &&
	test_i18ngrep -e "fatal: Cannot do soft reset with paths" err &&

	test_must_fail git reset --hard --pathspec-from-file=list 2>err &&
	test_i18ngrep -e "fatal: Cannot do hard reset with paths" err
'

test_done
