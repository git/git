#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test diff of symlinks.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

# Print the short OID of a symlink with the given name.
symlink_oid () {
	local oid=$(printf "%s" "$1" | git hash-object --stdin) &&
	git rev-parse --short "$oid"
}

# Print the short OID of the given file.
short_oid () {
	local oid=$(git hash-object "$1") &&
	git rev-parse --short "$oid"
}

test_expect_success 'diff new symlink and file' '
	symlink=$(symlink_oid xyzzy) &&
	cat >expected <<-EOF &&
	diff --git a/frotz b/frotz
	new file mode 120000
	index 0000000..$symlink
	--- /dev/null
	+++ b/frotz
	@@ -0,0 +1 @@
	+xyzzy
	\ No newline at end of file
	diff --git a/nitfol b/nitfol
	new file mode 100644
	index 0000000..$symlink
	--- /dev/null
	+++ b/nitfol
	@@ -0,0 +1 @@
	+xyzzy
	EOF

	# the empty tree
	git update-index &&
	tree=$(git write-tree) &&

	test_ln_s_add xyzzy frotz &&
	echo xyzzy >nitfol &&
	git update-index --add nitfol &&
	GIT_DIFF_OPTS=--unified=0 git diff-index -M -p $tree >current &&
	compare_diff_patch expected current
'

test_expect_success 'diff unchanged symlink and file'  '
	tree=$(git write-tree) &&
	git update-index frotz nitfol &&
	test -z "$(git diff-index --name-only $tree)"
'

test_expect_success 'diff removed symlink and file' '
	cat >expected <<-EOF &&
	diff --git a/frotz b/frotz
	deleted file mode 120000
	index $symlink..0000000
	--- a/frotz
	+++ /dev/null
	@@ -1 +0,0 @@
	-xyzzy
	\ No newline at end of file
	diff --git a/nitfol b/nitfol
	deleted file mode 100644
	index $symlink..0000000
	--- a/nitfol
	+++ /dev/null
	@@ -1 +0,0 @@
	-xyzzy
	EOF
	mv frotz frotz2 &&
	mv nitfol nitfol2 &&
	git diff-index -M -p $tree >current &&
	compare_diff_patch expected current
'

test_expect_success 'diff identical, but newly created symlink and file' '
	>expected &&
	rm -f frotz nitfol &&
	echo xyzzy >nitfol &&
	test-tool chmtime +10 nitfol &&
	if test_have_prereq SYMLINKS
	then
		ln -s xyzzy frotz
	else
		printf xyzzy >frotz
		# the symlink property propagates from the index
	fi &&
	git diff-index -M -p $tree >current &&
	compare_diff_patch expected current &&

	>expected &&
	git diff-index -M -p -w $tree >current &&
	compare_diff_patch expected current
'

test_expect_success 'diff different symlink and file' '
	new=$(symlink_oid yxyyz) &&
	cat >expected <<-EOF &&
	diff --git a/frotz b/frotz
	index $symlink..$new 120000
	--- a/frotz
	+++ b/frotz
	@@ -1 +1 @@
	-xyzzy
	\ No newline at end of file
	+yxyyz
	\ No newline at end of file
	diff --git a/nitfol b/nitfol
	index $symlink..$new 100644
	--- a/nitfol
	+++ b/nitfol
	@@ -1 +1 @@
	-xyzzy
	+yxyyz
	EOF
	rm -f frotz &&
	if test_have_prereq SYMLINKS
	then
		ln -s yxyyz frotz
	else
		printf yxyyz >frotz
		# the symlink property propagates from the index
	fi &&
	echo yxyyz >nitfol &&
	git diff-index -M -p $tree >current &&
	compare_diff_patch expected current
'

test_expect_success SYMLINKS 'diff symlinks with non-existing targets' '
	ln -s narf pinky &&
	ln -s take\ over brain &&
	test_must_fail git diff --no-index pinky brain >output 2>output.err &&
	grep narf output &&
	test_must_be_empty output.err
'

test_expect_success SYMLINKS 'setup symlinks with attributes' '
	echo "*.bin diff=bin" >>.gitattributes &&
	echo content >file.bin &&
	ln -s file.bin link.bin &&
	git add -N file.bin link.bin
'

test_expect_success SYMLINKS 'symlinks do not respect userdiff config by path' '
	file=$(short_oid file.bin) &&
	link=$(symlink_oid file.bin) &&
	cat >expect <<-EOF &&
	diff --git a/file.bin b/file.bin
	new file mode 100644
	index 0000000..$file
	Binary files /dev/null and b/file.bin differ
	diff --git a/link.bin b/link.bin
	new file mode 120000
	index 0000000..$link
	--- /dev/null
	+++ b/link.bin
	@@ -0,0 +1 @@
	+file.bin
	\ No newline at end of file
	EOF
	git config diff.bin.binary true &&
	git diff file.bin link.bin >actual &&
	test_cmp expect actual
'

test_done
