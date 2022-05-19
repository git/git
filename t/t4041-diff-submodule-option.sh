#!/bin/sh
#
# Copyright (c) 2009 Jens Lehmann, based on t7401 by Ping Yin
# Copyright (c) 2011 Alexey Shumkin (+ non-UTF-8 cummit encoding tests)
#

test_description='Support for verbose submodule differences in but diff

This test tries to verify the sanity of the --submodule option of but diff.
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Tested non-UTF-8 encoding
test_encoding="ISO8859-1"

# String "added" in German (translated with Google Translate), encoded in UTF-8,
# used in sample cummit log messages in add_file() function below.
added=$(printf "hinzugef\303\274gt")
add_file () {
	(
		cd "$1" &&
		shift &&
		for name
		do
			echo "$name" >"$name" &&
			but add "$name" &&
			test_tick &&
			# "but cummit -m" would break MinGW, as Windows refuse to pass
			# $test_encoding encoded parameter to but.
			echo "Add $name ($added $name)" | iconv -f utf-8 -t $test_encoding |
			but -c "i18n.cummitEncoding=$test_encoding" cummit -F -
		done >/dev/null &&
		but rev-parse --short --verify HEAD
	)
}
cummit_file () {
	test_tick &&
	but cummit "$@" -m "cummit $*" >/dev/null
}

test_create_repo sm1 &&
add_file . foo >/dev/null

head1=$(add_file sm1 foo1 foo2)
fullhead1=$(cd sm1; but rev-parse --verify HEAD)

test_expect_success 'added submodule' '
	but add sm1 &&
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$head1 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'added submodule, set diff.submodule' '
	but config diff.submodule log &&
	but add sm1 &&
	but diff --cached >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$head1 (new submodule)
	EOF
	but config --unset diff.submodule &&
	test_cmp expected actual
'

test_expect_success '--submodule=short overrides diff.submodule' '
	test_config diff.submodule log &&
	but add sm1 &&
	but diff --submodule=short --cached >actual &&
	cat >expected <<-EOF &&
	diff --but a/sm1 b/sm1
	new file mode 160000
	index 0000000..$head1
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+Subproject cummit $fullhead1
	EOF
	test_cmp expected actual
'

test_expect_success 'diff.submodule does not affect plumbing' '
	test_config diff.submodule log &&
	but diff-index -p HEAD >actual &&
	cat >expected <<-EOF &&
	diff --but a/sm1 b/sm1
	new file mode 160000
	index 0000000..$head1
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+Subproject cummit $fullhead1
	EOF
	test_cmp expected actual
'

cummit_file sm1 &&
head2=$(add_file sm1 foo3)

test_expect_success 'modified submodule(forward)' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule(forward)' '
	but diff --submodule=log >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule(forward) --submodule' '
	but diff --submodule >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

fullhead2=$(cd sm1; but rev-parse --verify HEAD)
test_expect_success 'modified submodule(forward) --submodule=short' '
	but diff --submodule=short >actual &&
	cat >expected <<-EOF &&
	diff --but a/sm1 b/sm1
	index $head1..$head2 160000
	--- a/sm1
	+++ b/sm1
	@@ -1 +1 @@
	-Subproject cummit $fullhead1
	+Subproject cummit $fullhead2
	EOF
	test_cmp expected actual
'

cummit_file sm1 &&
head3=$(
	cd sm1 &&
	but reset --hard HEAD~2 >/dev/null &&
	but rev-parse --short --verify HEAD
)

test_expect_success 'modified submodule(backward)' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head2..$head3 (rewind):
	  < Add foo3 ($added foo3)
	  < Add foo2 ($added foo2)
	EOF
	test_cmp expected actual
'

head4=$(add_file sm1 foo4 foo5)
test_expect_success 'modified submodule(backward and forward)' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head2...$head4:
	  > Add foo5 ($added foo5)
	  > Add foo4 ($added foo4)
	  < Add foo3 ($added foo3)
	  < Add foo2 ($added foo2)
	EOF
	test_cmp expected actual
'

cummit_file sm1 &&
mv sm1 sm1-bak &&
echo sm1 >sm1 &&
head5=$(but hash-object sm1 | cut -c1-7) &&
but add sm1 &&
rm -f sm1 &&
mv sm1-bak sm1

test_expect_success 'typechanged submodule(submodule->blob), --cached' '
	but diff --submodule=log --cached >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...0000000 (submodule deleted)
	diff --but a/sm1 b/sm1
	new file mode 100644
	index 0000000..$head5
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+sm1
	EOF
	test_cmp expected actual
'

test_expect_success 'typechanged submodule(submodule->blob)' '
	but diff --submodule=log >actual &&
	cat >expected <<-EOF &&
	diff --but a/sm1 b/sm1
	deleted file mode 100644
	index $head5..0000000
	--- a/sm1
	+++ /dev/null
	@@ -1 +0,0 @@
	-sm1
	Submodule sm1 0000000...$head4 (new submodule)
	EOF
	test_cmp expected actual
'

rm -rf sm1 &&
but checkout-index sm1
test_expect_success 'typechanged submodule(submodule->blob)' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...0000000 (submodule deleted)
	diff --but a/sm1 b/sm1
	new file mode 100644
	index 0000000..$head5
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+sm1
	EOF
	test_cmp expected actual
'

rm -f sm1 &&
test_create_repo sm1 &&
head6=$(add_file sm1 foo6 foo7)
fullhead6=$(cd sm1; but rev-parse --verify HEAD)
test_expect_success 'nonexistent cummit' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...$head6 (cummits not present)
	EOF
	test_cmp expected actual
'

cummit_file
test_expect_success 'typechanged submodule(blob->submodule)' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	diff --but a/sm1 b/sm1
	deleted file mode 100644
	index $head5..0000000
	--- a/sm1
	+++ /dev/null
	@@ -1 +0,0 @@
	-sm1
	Submodule sm1 0000000...$head6 (new submodule)
	EOF
	test_cmp expected actual
'

cummit_file sm1 &&
test_expect_success 'submodule is up to date' '
	but diff-index -p --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked content' '
	echo new > sm1/new-file &&
	but diff-index -p --ignore-submodules=none --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked content (untracked ignored)' '
	but diff-index -p --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked content (dirty ignored)' '
	but diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked content (all ignored)' '
	but diff-index -p --ignore-submodules=all --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked and modified content' '
	echo new > sm1/foo6 &&
	but diff-index -p --ignore-submodules=none --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 contains modified content
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked and modified content (untracked ignored)' '
	echo new > sm1/foo6 &&
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked and modified content (dirty ignored)' '
	echo new > sm1/foo6 &&
	but diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains untracked and modified content (all ignored)' '
	echo new > sm1/foo6 &&
	but diff-index -p --ignore-submodules --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'submodule contains modified content' '
	rm -f sm1/new-file &&
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	EOF
	test_cmp expected actual
'

(cd sm1; but cummit -mchange foo6 >/dev/null) &&
head8=$(cd sm1; but rev-parse --short --verify HEAD) &&
test_expect_success 'submodule is modified' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content' '
	echo new > sm1/new-file &&
	but diff-index -p  --ignore-submodules=none --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (untracked ignored)' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (dirty ignored)' '
	but diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (all ignored)' '
	but diff-index -p --ignore-submodules=all --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'modified submodule contains untracked and modified content' '
	echo modification >> sm1/foo6 &&
	but diff-index -p --ignore-submodules=none --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 contains modified content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modified content (untracked ignored)' '
	echo modification >> sm1/foo6 &&
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modified content (dirty ignored)' '
	echo modification >> sm1/foo6 &&
	but diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modified content (all ignored)' '
	echo modification >> sm1/foo6 &&
	but diff-index -p --ignore-submodules --submodule=log HEAD >actual &&
	test_must_be_empty actual
'

test_expect_success 'modified submodule contains modified content' '
	rm -f sm1/new-file &&
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

rm -rf sm1
test_expect_success 'deleted submodule' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	EOF
	test_cmp expected actual
'

test_expect_success 'create second submodule' '
	test_create_repo sm2 &&
	head7=$(add_file sm2 foo8 foo9) &&
	but add sm2
'

test_expect_success 'multiple submodules' '
	but diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'path filter' '
	but diff-index -p --submodule=log HEAD sm2 >actual &&
	cat >expected <<-EOF &&
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

cummit_file sm2
test_expect_success 'given cummit' '
	but diff-index -p --submodule=log HEAD^ >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'given cummit --submodule' '
	but diff-index -p --submodule HEAD^ >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

fullhead7=$(cd sm2; but rev-parse --verify HEAD)

test_expect_success 'given cummit --submodule=short' '
	but diff-index -p --submodule=short HEAD^ >actual &&
	cat >expected <<-EOF &&
	diff --but a/sm1 b/sm1
	deleted file mode 160000
	index $head6..0000000
	--- a/sm1
	+++ /dev/null
	@@ -1 +0,0 @@
	-Subproject cummit $fullhead6
	diff --but a/sm2 b/sm2
	new file mode 160000
	index 0000000..$head7
	--- /dev/null
	+++ b/sm2
	@@ -0,0 +1 @@
	+Subproject cummit $fullhead7
	EOF
	test_cmp expected actual
'

test_expect_success 'setup .but file for sm2' '
	(cd sm2 &&
	 REAL="$(pwd)/../.real" &&
	 mv .but "$REAL" &&
	 echo "butdir: $REAL" >.but)
'

test_expect_success 'diff --submodule with .but file' '
	but diff --submodule HEAD^ >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'diff --submodule with objects referenced by alternates' '
	mkdir sub_alt &&
	(cd sub_alt &&
		but init &&
		echo a >a &&
		but add a &&
		but cummit -m a
	) &&
	mkdir super &&
	(cd super &&
		but clone -s ../sub_alt sub &&
		but init &&
		but add sub &&
		but cummit -m "sub a"
	) &&
	(cd sub_alt &&
		sha1_before=$(but rev-parse --short HEAD) &&
		echo b >b &&
		but add b &&
		but cummit -m b &&
		sha1_after=$(but rev-parse --short HEAD) &&
		{
			echo "Submodule sub $sha1_before..$sha1_after:" &&
			echo "  > b"
		} >../expected
	) &&
	(cd super &&
		(cd sub &&
			but fetch &&
			but checkout origin/main
		) &&
		but diff --submodule > ../actual
	) &&
	test_cmp expected actual
'

test_done
