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

test_expect_success 'diff.context honored by "add"' '
	git add -p >output &&
	test_grep ! firstline output &&
	test_config diff.context 8 &&
	git log -1 -p >output &&
	test_grep "^ firstline" output
'

test_expect_success 'diff.context honored by "commit"' '
	! git commit -p >output &&
	test_grep ! firstline output &&
	test_config diff.context 8 &&
	! git commit -p >output &&
	test_grep "^ firstline" output
'

test_expect_success 'diff.context honored by "checkout"' '
	git checkout -p >output &&
	test_grep ! firstline output &&
	test_config diff.context 8 &&
	git checkout -p >output &&
	test_grep "^ firstline" output
'

test_expect_success 'diff.context honored by "stash"' '
	! git stash -p >output &&
	test_grep ! firstline output &&
	test_config diff.context 8 &&
	! git stash -p >output &&
	test_grep "^ firstline" output
'

test_expect_success 'diff.context honored by "restore"' '
	git restore -p >output &&
	test_grep ! firstline output &&
	test_config diff.context 8 &&
	git restore -p >output &&
	test_grep "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context' '
	test_config diff.context 8 &&
	git log -U4 -1 >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context for "add"' '
	test_config diff.context 8 &&
	git add -U4 -p >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context for "commit"' '
	test_config diff.context 8 &&
	! git commit -U4 -p >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context for "checkout"' '
	test_config diff.context 8 &&
	git checkout -U4 -p >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context for "stash"' '
	test_config diff.context 8 &&
	! git stash -U4 -p >output &&
	test_grep ! "^ firstline" output
'

test_expect_success 'The -U option overrides diff.context for "restore"' '
	test_config diff.context 8 &&
	git restore -U4 -p >output &&
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

test_expect_success 'negative integer config parsing by "add"' '
	test_config diff.context -1 &&
	test_must_fail git add -p 2>output &&
	test_grep "diff.context cannot be negative" output
'

test_expect_success 'negative integer config parsing by "commit"' '
	test_config diff.context -1 &&
	test_must_fail git commit -p 2>output &&
	test_grep "bad config variable" output
'

test_expect_success 'negative integer config parsing by "checkout"' '
	test_config diff.context -1 &&
	test_must_fail git checkout -p 2>output &&
	test_grep "diff.context cannot be negative" output
'

test_expect_success 'negative integer config parsing by "stash"' '
	test_config diff.context -1 &&
	test_must_fail git stash -p 2>output &&
	test_grep "diff.context cannot be negative" output
'

test_expect_success 'negative integer config parsing by "restore"' '
	test_config diff.context -1 &&
	test_must_fail git restore -p 2>output &&
	test_grep "diff.context cannot be negative" output
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
