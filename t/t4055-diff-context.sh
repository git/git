#!/bin/sh
#
# Copyright (c) 2012 Mozilla Foundation
#

test_description='diff.context configuration'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >template <<-\EOF &&
	firstline
	b
	c
	d
	e
	f
	preline
	TARGET
	postline
	i
	j
	k
	l
	m
	n
	EOF
	sed "/TARGET/d" >x <template &&
	git update-index --add x &&
	git commit -m initial &&

	sed "s/TARGET/ADDED/" >x <template &&
	git update-index --add x &&
	git commit -m next &&

	sed "s/TARGET/MODIFIED/" >x <template
'

test_expect_success 'the default number of context lines is 3' '
	git diff >output &&
	! grep "^ d" output &&
	grep "^ e" output &&
	grep "^ j" output &&
	! grep "^ k" output
'

test_expect_success 'diff.context honored by "log"' '
	git log -1 -p >output &&
	! grep firstline output &&
	git config diff.context 8 &&
	git log -1 -p >output &&
	grep "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context' '
	git config diff.context 8 &&
	git log -U4 -1 >output &&
	! grep "^ firstline" output
'

test_expect_success 'diff.context honored by "diff"' '
	git config diff.context 8 &&
	git diff >output &&
	grep "^ firstline" output
'

test_expect_success 'plumbing not affected' '
	git config diff.context 8 &&
	git diff-files -p >output &&
	! grep "^ firstline" output
'

test_expect_success 'non-integer config parsing' '
	git config diff.context no &&
	test_must_fail git diff 2>output &&
	test_i18ngrep "bad numeric config value" output
'

test_expect_success 'negative integer config parsing' '
	git config diff.context -1 &&
	test_must_fail git diff 2>output &&
	test_i18ngrep "bad config variable" output
'

test_expect_success '-U0 is valid, so is diff.context=0' '
	git config diff.context 0 &&
	git diff >output &&
	grep "^-ADDED" output &&
	grep "^+MODIFIED" output
'

test_done
