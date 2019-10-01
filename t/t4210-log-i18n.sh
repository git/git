#!/bin/sh

test_description='test log with i18n features'
. ./test-lib.sh

# two forms of é
utf8_e=$(printf '\303\251')
latin1_e=$(printf '\351')

test_expect_success 'create commits in different encodings' '
	test_tick &&
	cat >msg <<-EOF &&
	utf8

	t${utf8_e}st
	EOF
	git add msg &&
	git -c i18n.commitencoding=utf8 commit -F msg &&
	cat >msg <<-EOF &&
	latin1

	t${latin1_e}st
	EOF
	git add msg &&
	git -c i18n.commitencoding=ISO-8859-1 commit -F msg
'

test_expect_success 'log --grep searches in log output encoding (utf8)' '
	cat >expect <<-\EOF &&
	latin1
	utf8
	EOF
	git log --encoding=utf8 --format=%s --grep=$utf8_e >actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'log --grep searches in log output encoding (latin1)' '
	cat >expect <<-\EOF &&
	latin1
	utf8
	EOF
	git log --encoding=ISO-8859-1 --format=%s --grep=$latin1_e >actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'log --grep does not find non-reencoded values (utf8)' '
	git log --encoding=utf8 --format=%s --grep=$latin1_e >actual &&
	test_must_be_empty actual
'

test_expect_success 'log --grep does not find non-reencoded values (latin1)' '
	git log --encoding=ISO-8859-1 --format=%s --grep=$utf8_e >actual &&
	test_must_be_empty actual
'

test_done
