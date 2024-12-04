#!/bin/sh

test_description='git shell tests'

. ./test-lib.sh

test_expect_success 'shell allows upload-pack' '
	printf 0000 >input &&
	git upload-pack . <input >expect &&
	git shell -c "git-upload-pack $SQ.$SQ" <input >actual &&
	test_cmp expect actual
'

test_expect_success 'shell forbids other commands' '
	test_must_fail git shell -c "git config foo.bar baz"
'

test_expect_success 'shell forbids interactive use by default' '
	test_must_fail git shell
'

test_expect_success 'shell allows interactive command' '
	mkdir git-shell-commands &&
	write_script git-shell-commands/ping <<-\EOF &&
	echo pong
	EOF
	echo pong >expect &&
	echo ping | git shell >actual &&
	test_cmp expect actual
'

test_expect_success 'shell complains of overlong commands' '
	perl -e "print \"a\" x 2**12 for (0..2**19)" |
	test_must_fail git shell 2>err &&
	grep "too long" err
'

test_done
