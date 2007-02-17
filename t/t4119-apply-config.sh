#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='git-apply --whitespace=strip and configuration file.

'

. ./test-lib.sh

test_expect_success setup '
	echo A >file1 &&
	cp file1 saved &&
	git add file1 &&
	echo "B " >file1 &&
	git diff >patch.file
'

test_expect_success 'apply --whitespace=strip' '

	cp saved file1 &&
	git update-index --refresh &&

	git apply --whitespace=strip patch.file &&
	if grep " " file1
	then
		echo "Eh?"
		false
	else
		echo Happy
	fi
'

test_expect_success 'apply --whitespace=strip from config' '

	cp saved file1 &&
	git update-index --refresh &&

	git config apply.whitespace strip &&
	git apply patch.file &&
	if grep " " file1
	then
		echo "Eh?"
		false
	else
		echo Happy
	fi
'

mkdir sub
D=`pwd`

test_expect_success 'apply --whitespace=strip in subdir' '

	cd "$D" &&
	git config --unset-all apply.whitespace
	cp saved file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply --whitespace=strip ../patch.file &&
	if grep " " ../file1
	then
		echo "Eh?"
		false
	else
		echo Happy
	fi
'

test_expect_success 'apply --whitespace=strip from config in subdir' '

	cd "$D" &&
	git config apply.whitespace strip &&
	cp saved file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply ../patch.file &&
	if grep " " file1
	then
		echo "Eh?"
		false
	else
		echo Happy
	fi
'

test_done
