#!/bin/sh

test_description='apply to deeper directory without getting fooled with symlink'
. ./test-lib.sh

test_expect_success setup '

	mkdir -p arch/i386/boot arch/x86_64 &&
	test_write_lines 1 2 3 4 5 >arch/i386/boot/Makefile &&
	test_ln_s_add ../i386/boot arch/x86_64/boot &&
	git add . &&
	test_tick &&
	git commit -m initial &&
	git branch test &&

	rm arch/x86_64/boot &&
	mkdir arch/x86_64/boot &&
	test_write_lines 2 3 4 5 6 >arch/x86_64/boot/Makefile &&
	git add . &&
	test_tick &&
	git commit -a -m second &&

	git format-patch --binary -1 --stdout >test.patch

'

test_expect_success apply '

	git checkout test &&
	git diff --exit-code test &&
	git diff --exit-code --cached test &&
	git apply --index test.patch

'

test_expect_success 'check result' '

	git diff --exit-code master &&
	git diff --exit-code --cached master &&
	test_tick &&
	git commit -m replay &&
	T1=$(git rev-parse "master^{tree}") &&
	T2=$(git rev-parse "HEAD^{tree}") &&
	test "z$T1" = "z$T2"

'

test_done
