#!/bin/sh

test_description='diff --relative tests'
. ./test-lib.sh

test_expect_success 'setup' '
	git commit --allow-empty -m empty &&
	echo content >file1 &&
	mkdir subdir &&
	echo other content >subdir/file2 &&
	blob_file1=$(git hash-object file1) &&
	blob_file2=$(git hash-object subdir/file2) &&
	git add . &&
	git commit -m one
'

check_diff () {
	dir=$1
	shift
	expect=$1
	shift
	short_blob=$(git rev-parse --short $blob_file2)
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
		test_cmp expected actual
	"
}

check_raw () {
	dir=$1
	shift
	expect=$1
	shift
	cat >expected <<-EOF
	:000000 100644 $ZERO_OID $blob_file2 A	$expect
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

check_diff_relative_option () {
	dir=$1
	shift
	expect=$1
	shift
	relative_opt=$1
	shift
	test_expect_success "config diff.relative $relative_opt -p $*" "
		short_blob=\$(git rev-parse --short $blob_file2) &&
		cat >expected <<-EOF &&
		diff --git a/$expect b/$expect
		new file mode 100644
		index 0000000..\$short_blob
		--- /dev/null
		+++ b/$expect
		@@ -0,0 +1 @@
		+other content
		EOF
		test_config -C $dir diff.relative $relative_opt &&
		git -C '$dir' diff -p $* HEAD^ >actual &&
		test_cmp expected actual
	"
}

check_diff_no_relative_option () {
	dir=$1
	shift
	expect=$1
	shift
	relative_opt=$1
	shift
	test_expect_success "config diff.relative $relative_opt -p $*" "
		short_blob_file1=\$(git rev-parse --short $blob_file1) &&
		short_blob_file2=\$(git rev-parse --short $blob_file2) &&
		cat >expected <<-EOF &&
		diff --git a/file1 b/file1
		new file mode 100644
		index 0000000..\$short_blob_file1
		--- /dev/null
		+++ b/file1
		@@ -0,0 +1 @@
		+content
		diff --git a/$expect b/$expect
		new file mode 100644
		index 0000000..\$short_blob_file2
		--- /dev/null
		+++ b/$expect
		@@ -0,0 +1 @@
		+other content
		EOF
		test_config -C $dir diff.relative $relative_opt &&
		git -C '$dir' diff -p $* HEAD^ >actual &&
		test_cmp expected actual
	"
}

check_diff_no_relative_option . subdir/file2 false
check_diff_no_relative_option . subdir/file2 true --no-relative
check_diff_no_relative_option . subdir/file2 false --no-relative
check_diff_no_relative_option subdir subdir/file2 false
check_diff_no_relative_option subdir subdir/file2 true --no-relative
check_diff_no_relative_option subdir subdir/file2 false --no-relative

check_diff_relative_option . file2 false --relative=subdir/
check_diff_relative_option . file2 false --relative=subdir
check_diff_relative_option . file2 true --relative=subdir/
check_diff_relative_option . file2 true --relative=subdir
check_diff_relative_option subdir file2 false --relative
check_diff_relative_option subdir file2 true --relative
check_diff_relative_option subdir file2 true
check_diff_relative_option subdir file2 false --no-relative --relative
check_diff_relative_option subdir file2 true --no-relative --relative
check_diff_relative_option . file2 false --no-relative --relative=subdir
check_diff_relative_option . file2 true --no-relative --relative=subdir

test_expect_success 'setup diff --relative unmerged' '
	test_commit zero file0 &&
	test_commit base subdir/file0 &&
	git switch -c br1 &&
	test_commit one file0 &&
	test_commit sub1 subdir/file0 &&
	git switch -c br2 base &&
	test_commit two file0 &&
	git switch -c br3 &&
	test_commit sub3 subdir/file0
'

test_expect_success 'diff --relative without change in subdir' '
	git switch br2 &&
	test_when_finished "git merge --abort" &&
	test_must_fail git merge one &&
	git -C subdir diff --relative >out &&
	test_must_be_empty out &&
	git -C subdir diff --relative --name-only >out &&
	test_must_be_empty out
'

test_expect_success 'diff --relative --name-only with change in subdir' '
	git switch br3 &&
	test_when_finished "git merge --abort" &&
	test_must_fail git merge sub1 &&
	test_write_lines file0 file0 >expected &&
	git -C subdir diff --relative --name-only >out &&
	test_cmp expected out
'

test_expect_failure 'diff --relative with change in subdir' '
	git switch br3 &&
	br1_blob=$(git rev-parse --short --verify br1:subdir/file0) &&
	br3_blob=$(git rev-parse --short --verify br3:subdir/file0) &&
	test_when_finished "git merge --abort" &&
	test_must_fail git merge br1 &&
	cat >expected <<-EOF &&
	diff --cc file0
	index $br3_blob,$br1_blob..0000000
	--- a/file0
	+++ b/file0
	@@@ -1,1 -1,1 +1,5 @@@
	++<<<<<<< HEAD
	 +sub3
	++=======
	+ sub1
	++>>>>>>> br1
	EOF
	git -C subdir diff --relative >out &&
	test_cmp expected out
'

test_done
