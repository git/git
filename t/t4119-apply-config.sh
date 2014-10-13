#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='git apply --whitespace=strip and configuration file.

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
tr '_' ' ' >gpatch.file <<\EOF &&
--- file1	2007-02-21 01:04:24.000000000 -0800
+++ file1+	2007-02-21 01:07:44.000000000 -0800
@@ -1 +1 @@
-A
+B_
EOF

sed -e 's|file1|sub/&|' gpatch.file >gpatch-sub.file &&
sed -e '
	/^--- /s|file1|a/sub/&|
	/^+++ /s|file1|b/sub/&|
' gpatch.file >gpatch-ab-sub.file &&

check_result () {
	if grep " " "$1"
	then
		echo "Eh?"
		false
	elif grep B "$1"
	then
		echo Happy
	else
		echo "Huh?"
		false
	fi
}

test_expect_success 'apply --whitespace=strip' '

	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	git apply --whitespace=strip patch.file &&
	check_result sub/file1
'

test_expect_success 'apply --whitespace=strip from config' '

	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	git config apply.whitespace strip &&
	git apply patch.file &&
	check_result sub/file1
'

D=$(pwd)

test_expect_success 'apply --whitespace=strip in subdir' '

	cd "$D" &&
	git config --unset-all apply.whitespace &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply --whitespace=strip ../patch.file &&
	check_result file1
'

test_expect_success 'apply --whitespace=strip from config in subdir' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply ../patch.file &&
	check_result file1
'

test_expect_success 'same in subdir but with traditional patch input' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply ../gpatch.file &&
	check_result file1
'

test_expect_success 'same but with traditional patch input of depth 1' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply ../gpatch-sub.file &&
	check_result file1
'

test_expect_success 'same but with traditional patch input of depth 2' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply ../gpatch-ab-sub.file &&
	check_result file1
'

test_expect_success 'same but with traditional patch input of depth 1' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	git apply -p0 gpatch-sub.file &&
	check_result sub/file1
'

test_expect_success 'same but with traditional patch input of depth 2' '

	cd "$D" &&
	git config apply.whitespace strip &&
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	git apply gpatch-ab-sub.file &&
	check_result sub/file1
'

test_expect_success 'in subdir with traditional patch input' '
	cd "$D" &&
	git config apply.whitespace strip &&
	cat >.gitattributes <<-EOF &&
	/* whitespace=blank-at-eol
	sub/* whitespace=-blank-at-eol
	EOF
	rm -f sub/file1 &&
	cp saved sub/file1 &&
	git update-index --refresh &&

	cd sub &&
	git apply ../gpatch.file &&
	echo "B " >expect &&
	test_cmp expect file1
'

test_done
