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
	test_grep ! "^ d" output &&
	test_grep "^ e" output &&
	test_grep "^ j" output &&
	test_grep ! "^ k" output
'

test_expect_success 'diff.context honored by "log"' '
	git log -1 -p >output &&
	test_grep ! firstline output &&
	test_config diff.context 8 &&
	git log -1 -p >output &&
	test_grep "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context' '
	test_config diff.context 8 &&
	git log -U4 -1 >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'diff.context honored by "diff"' '
	test_config diff.context 8 &&
	git diff >output &&
	test_grep "^ firstline" output
'

test_expect_success 'plumbing not affected' '
	test_config diff.context 8 &&
	git diff-files -p >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'non-integer config parsing' '
	test_config diff.context no &&
	test_must_fail git diff 2>output &&
	test_grep "bad numeric config value" output
'

test_expect_success 'negative integer config parsing' '
	test_config diff.context -1 &&
	test_must_fail git diff 2>output &&
	test_grep "bad config variable" output
'

test_expect_success '-U0 is valid, so is diff.context=0' '
	test_config diff.context 0 &&
	git diff >output &&
	test_grep "^-ADDED" output &&
	test_grep "^+MODIFIED" output
'

test_expect_success '-U2147483647 works' '
	echo APPENDED >>x &&
	test_line_count = 16 x &&
	git diff -U2147483647 >output &&
	test_line_count = 22 output &&
	test_grep "^-ADDED" output &&
	test_grep "^+MODIFIED" output &&
	test_grep "^+APPENDED" output
'

test_done
