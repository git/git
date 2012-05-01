#!/bin/sh

test_description='diff --relative tests'
. ./test-lib.sh

test_expect_success 'setup' '
	git commit --allow-empty -m empty &&
	echo content >file1 &&
	mkdir subdir &&
	echo other content >subdir/file2 &&
	git add . &&
	git commit -m one
'

check_diff() {
expect=$1; shift
cat >expected <<EOF
diff --git a/$expect b/$expect
new file mode 100644
index 0000000..25c05ef
--- /dev/null
+++ b/$expect
@@ -0,0 +1 @@
+other content
EOF
test_expect_success "-p $*" "
	git diff -p $* HEAD^ >actual &&
	test_cmp expected actual
"
}

check_numstat() {
expect=$1; shift
cat >expected <<EOF
1	0	$expect
EOF
test_expect_success "--numstat $*" "
	echo '1	0	$expect' >expected &&
	git diff --numstat $* HEAD^ >actual &&
	test_cmp expected actual
"
}

check_stat() {
expect=$1; shift
cat >expected <<EOF
 $expect | 1 +
 1 file changed, 1 insertion(+)
EOF
test_expect_success "--stat $*" "
	git diff --stat $* HEAD^ >actual &&
	test_i18ncmp expected actual
"
}

check_raw() {
expect=$1; shift
cat >expected <<EOF
:000000 100644 0000000000000000000000000000000000000000 25c05ef3639d2d270e7fe765a67668f098092bc5 A	$expect
EOF
test_expect_success "--raw $*" "
	git diff --no-abbrev --raw $* HEAD^ >actual &&
	test_cmp expected actual
"
}

for type in diff numstat stat raw; do
	check_$type file2 --relative=subdir/
	check_$type file2 --relative=subdir
	check_$type dir/file2 --relative=sub
done

test_done
