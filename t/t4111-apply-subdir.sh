#!/bin/sh

test_description='patching from inconvenient places'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >patch <<-\EOF &&
	diff file.orig file
	--- a/file.orig
	+++ b/file
	@@ -1 +1,2 @@
	 1
	+2
	EOF
	patch="$(pwd)/patch" &&

	echo 1 >preimage &&
	printf "%s\n" 1 2 >postimage &&
	echo 3 >other &&

	test_tick &&
	but cummit --allow-empty -m basis
'

test_expect_success 'setup: subdir' '
	reset_subdir() {
		but reset &&
		mkdir -p sub/dir/b &&
		mkdir -p objects &&
		cp "$1" file &&
		cp "$1" objects/file &&
		cp "$1" sub/dir/file &&
		cp "$1" sub/dir/b/file &&
		but add file sub/dir/file sub/dir/b/file objects/file &&
		cp "$2" file &&
		cp "$2" sub/dir/file &&
		cp "$2" sub/dir/b/file &&
		cp "$2" objects/file &&
		test_might_fail but update-index --refresh -q
	}
'

test_expect_success 'apply from subdir of toplevel' '
	cp postimage expected &&
	reset_subdir other preimage &&
	(
		cd sub/dir &&
		but apply "$patch"
	) &&
	test_cmp expected sub/dir/file
'

test_expect_success 'apply --cached from subdir of toplevel' '
	cp postimage expected &&
	cp other expected.working &&
	reset_subdir preimage other &&
	(
		cd sub/dir &&
		but apply --cached "$patch"
	) &&
	but show :sub/dir/file >actual &&
	test_cmp expected actual &&
	test_cmp expected.working sub/dir/file
'

test_expect_success 'apply --index from subdir of toplevel' '
	cp postimage expected &&
	reset_subdir preimage other &&
	(
		cd sub/dir &&
		test_must_fail but apply --index "$patch"
	) &&
	reset_subdir other preimage &&
	(
		cd sub/dir &&
		test_must_fail but apply --index "$patch"
	) &&
	reset_subdir preimage preimage &&
	(
		cd sub/dir &&
		but apply --index "$patch"
	) &&
	but show :sub/dir/file >actual &&
	test_cmp expected actual &&
	test_cmp expected sub/dir/file
'

test_expect_success 'apply half-broken patch from subdir of toplevel' '
	(
		cd sub/dir &&
		test_must_fail but apply <<-EOF
		--- sub/dir/file
		+++ sub/dir/file
		@@ -1,0 +1,0 @@
		--- file_in_root
		+++ file_in_root
		@@ -1,0 +1,0 @@
		EOF
	)
'

test_expect_success 'apply from .but dir' '
	cp postimage expected &&
	cp preimage .but/file &&
	cp preimage .but/objects/file &&
	(
		cd .but &&
		but apply "$patch"
	) &&
	test_cmp expected .but/file
'

test_expect_success 'apply from subdir of .but dir' '
	cp postimage expected &&
	cp preimage .but/file &&
	cp preimage .but/objects/file &&
	(
		cd .but/objects &&
		but apply "$patch"
	) &&
	test_cmp expected .but/objects/file
'

test_expect_success 'apply --cached from .but dir' '
	cp postimage expected &&
	cp other expected.working &&
	cp other .but/file &&
	reset_subdir preimage other &&
	(
		cd .but &&
		but apply --cached "$patch"
	) &&
	but show :file >actual &&
	test_cmp expected actual &&
	test_cmp expected.working file &&
	test_cmp expected.working .but/file
'

test_expect_success 'apply --cached from subdir of .but dir' '
	cp postimage expected &&
	cp preimage expected.subdir &&
	cp other .but/file &&
	cp other .but/objects/file &&
	reset_subdir preimage other &&
	(
		cd .but/objects &&
		but apply --cached "$patch"
	) &&
	but show :file >actual &&
	but show :objects/file >actual.subdir &&
	test_cmp expected actual &&
	test_cmp expected.subdir actual.subdir
'

test_done
