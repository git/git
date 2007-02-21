#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='git-apply --whitespace=strip and configuration file.

'

. ./test-lib.sh

test_expect_success setup '
	mkdir sub &&
	echo A >sub/file1 &&
	cp sub/file1 saved &&
	git add sub/file1 &&
	echo "B " >sub/file1 &&
	git diff >patch.file
'

# Also handcraft GNU diff output; note this has trailing whitespace.
cat >gpatch.file <<\EOF
--- file1	2007-02-21 01:04:24.000000000 -0800
+++ file1+	2007-02-21 01:07:44.000000000 -0800
@@ -1 +1 @@
-A
+B 
EOF

test_expect_success 'apply --whitespace=strip' '

	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	git apply --whitespace=strip patch.file &&
	if grep " " sub/file1
	then
		echo "Eh?"
		false
	elif grep B sub/file1
	then
		echo Happy
	else
		echo "Huh?"
		false
	fi
'

test_expect_success 'apply --whitespace=strip from config' '

	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	git config apply.whitespace strip &&
	git apply patch.file &&
	if grep " " sub/file1
	then
		echo "Eh?"
		false
	elif grep B sub/file1
	then
		echo Happy
	else
		echo Happy
	fi
'

D=`pwd`

test_expect_success 'apply --whitespace=strip in subdir' '

	cd "$D" &&
	git config --unset-all apply.whitespace
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply --whitespace=strip -p2 ../patch.file &&
	if grep " " file1
	then
		echo "Eh?"
		false
	elif grep B file1
	then
		echo Happy
	else
		echo "Huh?"
		false
	fi
'

test_expect_success 'apply --whitespace=strip from config in subdir' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply -p2 ../patch.file &&
	if grep " " file1
	then
		echo "Eh?"
		false
	elif grep B file1
	then
		echo Happy
	else
		echo "Huh?"
		false
	fi
'

test_expect_success 'same in subdir but with traditional patch input' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply -p0 ../gpatch.file &&
	if grep " " file1
	then
		echo "Eh?"
		false
	elif grep B file1
	then
		echo Happy
	else
		echo "Huh?"
		false
	fi
'

test_done
