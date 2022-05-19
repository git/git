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
	but update-index --add x &&
	but cummit -m initial &&

	sed "s/TARGET/ADDED/" >x <template &&
	but update-index --add x &&
	but cummit -m next &&

	sed "s/TARGET/MODIFIED/" >x <template
'

test_expect_success 'the default number of context lines is 3' '
	but diff >output &&
	! grep "^ d" output &&
	grep "^ e" output &&
	grep "^ j" output &&
	! grep "^ k" output
'

test_expect_success 'diff.context honored by "log"' '
	but log -1 -p >output &&
	! grep firstline output &&
	but config diff.context 8 &&
	but log -1 -p >output &&
	grep "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context' '
	but config diff.context 8 &&
	but log -U4 -1 >output &&
	! grep "^ firstline" output
'

test_expect_success 'diff.context honored by "diff"' '
	but config diff.context 8 &&
	but diff >output &&
	grep "^ firstline" output
'

test_expect_success 'plumbing not affected' '
	but config diff.context 8 &&
	but diff-files -p >output &&
	! grep "^ firstline" output
'

test_expect_success 'non-integer config parsing' '
	but config diff.context no &&
	test_must_fail but diff 2>output &&
	test_i18ngrep "bad numeric config value" output
'

test_expect_success 'negative integer config parsing' '
	but config diff.context -1 &&
	test_must_fail but diff 2>output &&
	test_i18ngrep "bad config variable" output
'

test_expect_success '-U0 is valid, so is diff.context=0' '
	but config diff.context 0 &&
	but diff >output &&
	grep "^-ADDED" output &&
	grep "^+MODIFIED" output
'

test_done
