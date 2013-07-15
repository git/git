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
	git commit --allow-empty -m basis
'

test_expect_success 'setup: subdir' '
	reset_subdir() {
		git reset &&
		mkdir -p sub/dir/b &&
		mkdir -p objects &&
		cp "$1" file &&
		cp "$1" objects/file &&
		cp "$1" sub/dir/file &&
		cp "$1" sub/dir/b/file &&
		git add file sub/dir/file sub/dir/b/file objects/file &&
		cp "$2" file &&
		cp "$2" sub/dir/file &&
		cp "$2" sub/dir/b/file &&
		cp "$2" objects/file &&
		test_might_fail git update-index --refresh -q
	}
'

test_expect_success 'apply from subdir of toplevel' '
	cp postimage expected &&
	reset_subdir other preimage &&
	(
		cd sub/dir &&
		git apply "$patch"
	) &&
	test_cmp expected sub/dir/file
'

test_expect_success 'apply --cached from subdir of toplevel' '
	cp postimage expected &&
	cp other expected.working &&
	reset_subdir preimage other &&
	(
		cd sub/dir &&
		git apply --cached "$patch"
	) &&
	git show :sub/dir/file >actual &&
	test_cmp expected actual &&
	test_cmp expected.working sub/dir/file
'

test_expect_success 'apply --index from subdir of toplevel' '
	cp postimage expected &&
	reset_subdir preimage other &&
	(
		cd sub/dir &&
		test_must_fail git apply --index "$patch"
	) &&
	reset_subdir other preimage &&
	(
		cd sub/dir &&
		test_must_fail git apply --index "$patch"
	) &&
	reset_subdir preimage preimage &&
	(
		cd sub/dir &&
		git apply --index "$patch"
	) &&
	git show :sub/dir/file >actual &&
	test_cmp expected actual &&
	test_cmp expected sub/dir/file
'

test_expect_success 'apply half-broken patch from subdir of toplevel' '
	(
		cd sub/dir &&
		test_must_fail git apply <<-EOF
		--- sub/dir/file
		+++ sub/dir/file
		@@ -1,0 +1,0 @@
		--- file_in_root
		+++ file_in_root
		@@ -1,0 +1,0 @@
		EOF
	)
'

test_expect_success 'apply from .git dir' '
	cp postimage expected &&
	cp preimage .git/file &&
	cp preimage .git/objects/file &&
	(
		cd .git &&
		git apply "$patch"
	) &&
	test_cmp expected .git/file
'

test_expect_success 'apply from subdir of .git dir' '
	cp postimage expected &&
	cp preimage .git/file &&
	cp preimage .git/objects/file &&
	(
		cd .git/objects &&
		git apply "$patch"
	) &&
	test_cmp expected .git/objects/file
'

test_expect_success 'apply --cached from .git dir' '
	cp postimage expected &&
	cp other expected.working &&
	cp other .git/file &&
	reset_subdir preimage other &&
	(
		cd .git &&
		git apply --cached "$patch"
	) &&
	git show :file >actual &&
	test_cmp expected actual &&
	test_cmp expected.working file &&
	test_cmp expected.working .git/file
'

test_expect_success 'apply --cached from subdir of .git dir' '
	cp postimage expected &&
	cp preimage expected.subdir &&
	cp other .git/file &&
	cp other .git/objects/file &&
	reset_subdir preimage other &&
	(
		cd .git/objects &&
		git apply --cached "$patch"
	) &&
	git show :file >actual &&
	git show :objects/file >actual.subdir &&
	test_cmp expected actual &&
	test_cmp expected.subdir actual.subdir
'

test_done
