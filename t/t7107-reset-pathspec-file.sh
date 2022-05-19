#!/bin/sh

test_description='reset --pathspec-from-file'

. ./test-lib.sh

test_tick

test_expect_success setup '
	echo A >fileA.t &&
	echo B >fileB.t &&
	echo C >fileC.t &&
	echo D >fileD.t &&
	but add . &&
	but cummit --include . -m "cummit" &&
	but tag checkpoint
'

restore_checkpoint () {
	but reset --hard checkpoint
}

verify_expect () {
	but status --porcelain -- fileA.t fileB.t fileC.t fileD.t >actual &&
	if test "x$1" = 'x!'
	then
		! test_cmp expect actual
	else
		test_cmp expect actual
	fi
}

test_expect_success '--pathspec-from-file from stdin' '
	restore_checkpoint &&

	but rm fileA.t &&
	echo fileA.t | but reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect
'

test_expect_success '--pathspec-from-file from file' '
	restore_checkpoint &&

	but rm fileA.t &&
	echo fileA.t >list &&
	but reset --pathspec-from-file=list &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect
'

test_expect_success 'NUL delimiters' '
	restore_checkpoint &&

	but rm fileA.t fileB.t &&
	printf "fileA.t\0fileB.t\0" | but reset --pathspec-from-file=- --pathspec-file-nul &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'LF delimiters' '
	restore_checkpoint &&

	but rm fileA.t fileB.t &&
	printf "fileA.t\nfileB.t\n" | but reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'no trailing delimiter' '
	restore_checkpoint &&

	but rm fileA.t fileB.t &&
	printf "fileA.t\nfileB.t" | but reset --pathspec-from-file=- &&

	cat >expect <<-\EOF &&
	 D fileA.t
	 D fileB.t
	EOF
	verify_expect
'

test_expect_success 'CRLF delimiters' '
	restore_checkpoint &&

	but rm fileA.t fileB.t &&
	printf "fileA.t\r\nfileB.t\r\n" | but reset --pathspec-from-file=- &&

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

	but rm fileA.t &&
	but reset --pathspec-from-file=list &&

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

	# Note: "but reset" has not yet learned to fail on wrong pathspecs
	but reset --pathspec-from-file=list --pathspec-file-nul &&

	cat >expect <<-\EOF &&
	 D fileA.t
	EOF
	verify_expect !
'

test_expect_success 'only touches what was listed' '
	restore_checkpoint &&

	but rm fileA.t fileB.t fileC.t fileD.t &&
	printf "fileB.t\nfileC.t\n" | but reset --pathspec-from-file=- &&

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
	but rm fileA.t &&

	test_must_fail but reset --pathspec-from-file=list --patch 2>err &&
	test_i18ngrep -e "options .--pathspec-from-file. and .--patch. cannot be used together" err &&

	test_must_fail but reset --pathspec-from-file=list -- fileA.t 2>err &&
	test_i18ngrep -e ".--pathspec-from-file. and pathspec arguments cannot be used together" err &&

	test_must_fail but reset --pathspec-file-nul 2>err &&
	test_i18ngrep -e "the option .--pathspec-file-nul. requires .--pathspec-from-file." err &&

	test_must_fail but reset --soft --pathspec-from-file=list 2>err &&
	test_i18ngrep -e "fatal: Cannot do soft reset with paths" err &&

	test_must_fail but reset --hard --pathspec-from-file=list 2>err &&
	test_i18ngrep -e "fatal: Cannot do hard reset with paths" err
'

test_done
