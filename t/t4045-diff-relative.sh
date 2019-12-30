#!/bin/sh

test_description='diff --relative tests'
. ./test-lib.sh

test_expect_success 'setup' '
	git commit --allow-empty -m empty &&
	echo content >file1 &&
	mkdir subdir &&
	echo other content >subdir/file2 &&
	blob=$(git hash-object subdir/file2) &&
	git add . &&
	git commit -m one
'

check_diff () {
	dir=$1
	shift
	expect=$1
	shift
	short_blob=$(git rev-parse --short $blob)
	cat >expected <<-EOF
	diff --git a/$expect b/$expect
	new file mode 100644
	index 0000000..$short_blob
	--- /dev/null
	+++ b/$expect
	@@ -0,0 +1 @@
	+other content
	EOF
	test_expect_success "-p $*" "
		git -C '$dir' diff -p $* HEAD^ >actual &&
		test_cmp expected actual
	"
}

check_numstat () {
	dir=$1
	shift
	expect=$1
	shift
	cat >expected <<-EOF
	1	0	$expect
	EOF
	test_expect_success "--numstat $*" "
		echo '1	0	$expect' >expected &&
		git -C '$dir' diff --numstat $* HEAD^ >actual &&
		test_cmp expected actual
	"
}

check_stat () {
	dir=$1
	shift
	expect=$1
	shift
	cat >expected <<-EOF
	 $expect | 1 +
	 1 file changed, 1 insertion(+)
	EOF
	test_expect_success "--stat $*" "
		git -C '$dir' diff --stat $* HEAD^ >actual &&
		test_i18ncmp expected actual
	"
}

check_raw () {
	dir=$1
	shift
	expect=$1
	shift
	cat >expected <<-EOF
	:000000 100644 $ZERO_OID $blob A	$expect
	EOF
	test_expect_success "--raw $*" "
		git -C '$dir' diff --no-abbrev --raw $* HEAD^ >actual &&
		test_cmp expected actual
	"
}

for type in diff numstat stat raw
do
	check_$type . file2 --relative=subdir/
	check_$type . file2 --relative=subdir
	check_$type subdir file2 --relative
	check_$type . dir/file2 --relative=sub
done

test_done
