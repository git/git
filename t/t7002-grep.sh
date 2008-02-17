#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git grep various.
'

. ./test-lib.sh

test_expect_success setup '
	{
		echo foo mmap bar
		echo foo_mmap bar
		echo foo_mmap bar mmap
		echo foo mmap bar_mmap
		echo foo_mmap bar mmap baz
	} >file &&
	echo x x xx x >x &&
	echo y yy >y &&
	echo zzz > z &&
	mkdir t &&
	echo test >t/t &&
	git add file x y z t/t &&
	git commit -m initial
'

for H in HEAD ''
do
	case "$H" in
	HEAD)	HC='HEAD:' L='HEAD' ;;
	'')	HC= L='in working tree' ;;
	esac

	test_expect_success "grep -w $L" '
		{
			echo ${HC}file:1:foo mmap bar
			echo ${HC}file:3:foo_mmap bar mmap
			echo ${HC}file:4:foo mmap bar_mmap
			echo ${HC}file:5:foo_mmap bar mmap baz
		} >expected &&
		git grep -n -w -e mmap $H >actual &&
		diff expected actual
	'

	test_expect_success "grep -w $L (x)" '
		{
			echo ${HC}x:1:x x xx x
		} >expected &&
		git grep -n -w -e "x xx* x" $H >actual &&
		diff expected actual
	'

	test_expect_success "grep -w $L (y-1)" '
		{
			echo ${HC}y:1:y yy
		} >expected &&
		git grep -n -w -e "^y" $H >actual &&
		diff expected actual
	'

	test_expect_success "grep -w $L (y-2)" '
		: >expected &&
		if git grep -n -w -e "^y y" $H >actual
		then
			echo should not have matched
			cat actual
			false
		else
			diff expected actual
		fi
	'

	test_expect_success "grep -w $L (z)" '
		: >expected &&
		if git grep -n -w -e "^z" $H >actual
		then
			echo should not have matched
			cat actual
			false
		else
			diff expected actual
		fi
	'

	test_expect_success "grep $L (t-1)" '
		echo "${HC}t/t:1:test" >expected &&
		git grep -n -e test $H >actual &&
		diff expected actual
	'

	test_expect_success "grep $L (t-2)" '
		echo "${HC}t:1:test" >expected &&
		(
			cd t &&
			git grep -n -e test $H
		) >actual &&
		diff expected actual
	'

	test_expect_success "grep $L (t-3)" '
		echo "${HC}t/t:1:test" >expected &&
		(
			cd t &&
			git grep --full-name -n -e test $H
		) >actual &&
		diff expected actual
	'

	test_expect_success "grep -c $L (no /dev/null)" '
		! git grep -c test $H | grep -q /dev/null
        '

done

test_done
