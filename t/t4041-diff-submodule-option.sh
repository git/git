#!/bin/sh
#
# Copyright (c) 2009 Jens Lehmann, based on t7401 by Ping Yin
#

test_description='Support for verbose submodule differences in git diff

This test tries to verify the sanity of the --submodule option of git diff.
'

. ./test-lib.sh

add_file () {
	(
		cd "$1" &&
		shift &&
		for name
		do
			echo "$name" >"$name" &&
			git add "$name" &&
			test_tick &&
			git commit -m "Add $name" || exit
		done >/dev/null &&
		git rev-parse --short --verify HEAD
	)
}
commit_file () {
	test_tick &&
	git commit "$@" -m "Commit $*" >/dev/null
}

test_create_repo sm1 &&
add_file . foo >/dev/null

head1=$(add_file sm1 foo1 foo2)
fullhead1=$(cd sm1; git rev-parse --verify HEAD)

test_expect_success 'added submodule' '
	git add sm1 &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$head1 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'added submodule, set diff.submodule' '
	git config diff.submodule log &&
	git add sm1 &&
	git diff --cached >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$head1 (new submodule)
	EOF
	git config --unset diff.submodule &&
	test_cmp expected actual
'

test_expect_success '--submodule=short overrides diff.submodule' '
	test_config diff.submodule log &&
	git add sm1 &&
	git diff --submodule=short --cached >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	new file mode 160000
	index 0000000..$head1
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+Subproject commit $fullhead1
	EOF
	test_cmp expected actual
'

test_expect_success 'diff.submodule does not affect plumbing' '
	test_config diff.submodule log &&
	git diff-index -p HEAD >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	new file mode 160000
	index 0000000..$head1
	--- /dev/null
	+++ b/sm1
	@@ -0,0 +1 @@
	+Subproject commit $fullhead1
	EOF
	test_cmp expected actual
'

commit_file sm1 &&
head2=$(add_file sm1 foo3)

test_expect_success 'modified submodule(forward)' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule(forward)' '
	git diff --submodule=log >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule(forward) --submodule' '
	git diff --submodule >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head1..$head2:
	  > Add foo3
	EOF
	test_cmp expected actual
'

fullhead2=$(cd sm1; git rev-parse --verify HEAD)
test_expect_success 'modified submodule(forward) --submodule=short' '
	git diff --submodule=short >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	index $head1..$head2 160000
	--- a/sm1
	+++ b/sm1
	@@ -1 +1 @@
	-Subproject commit $fullhead1
	+Subproject commit $fullhead2
	EOF
	test_cmp expected actual
'

commit_file sm1 &&
head3=$(
	cd sm1 &&
	git reset --hard HEAD~2 >/dev/null &&
	git rev-parse --short --verify HEAD
)

test_expect_success 'modified submodule(backward)' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head2..$head3 (rewind):
	  < Add foo3
	  < Add foo2
	EOF
	test_cmp expected actual
'

head4=$(add_file sm1 foo4 foo5)
test_expect_success 'modified submodule(backward and forward)' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head2...$head4:
	  > Add foo5
	  > Add foo4
	  < Add foo3
	  < Add foo2
	EOF
	test_cmp expected actual
'

commit_file sm1 &&
mv sm1 sm1-bak &&
echo sm1 >sm1 &&
head5=$(git hash-object sm1 | cut -c1-7) &&
git add sm1 &&
rm -f sm1 &&
mv sm1-bak sm1

test_expect_success 'typechanged submodule(submodule->blob), --cached' '
	git diff --submodule=log --cached >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...0000000 (submodule deleted)
	diff --git a/sm1 b/sm1
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
	git diff --submodule=log >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
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
git checkout-index sm1
test_expect_success 'typechanged submodule(submodule->blob)' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...0000000 (submodule deleted)
	diff --git a/sm1 b/sm1
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
fullhead6=$(cd sm1; git rev-parse --verify HEAD)
test_expect_success 'nonexistent commit' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head4...$head6 (commits not present)
	EOF
	test_cmp expected actual
'

commit_file
test_expect_success 'typechanged submodule(blob->submodule)' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
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

commit_file sm1 &&
test_expect_success 'submodule is up to date' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked content' '
	echo new > sm1/new-file &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked content (untracked ignored)' '
	git diff-index -p --ignore-submodules=untracked --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'submodule contains untracked content (dirty ignored)' '
	git diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'submodule contains untracked content (all ignored)' '
	git diff-index -p --ignore-submodules=all --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'submodule contains untracked and modifed content' '
	echo new > sm1/foo6 &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 contains modified content
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked and modifed content (untracked ignored)' '
	echo new > sm1/foo6 &&
	git diff-index -p --ignore-submodules=untracked --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule contains untracked and modifed content (dirty ignored)' '
	echo new > sm1/foo6 &&
	git diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'submodule contains untracked and modifed content (all ignored)' '
	echo new > sm1/foo6 &&
	git diff-index -p --ignore-submodules --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'submodule contains modifed content' '
	rm -f sm1/new-file &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	EOF
	test_cmp expected actual
'

(cd sm1; git commit -mchange foo6 >/dev/null) &&
head8=$(cd sm1; git rev-parse --short --verify HEAD) &&
test_expect_success 'submodule is modified' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content' '
	echo new > sm1/new-file &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (untracked ignored)' '
	git diff-index -p --ignore-submodules=untracked --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (dirty ignored)' '
	git diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked content (all ignored)' '
	git diff-index -p --ignore-submodules=all --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'modified submodule contains untracked and modifed content' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains untracked content
	Submodule sm1 contains modified content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modifed content (untracked ignored)' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --ignore-submodules=untracked --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modifed content (dirty ignored)' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --ignore-submodules=dirty --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

test_expect_success 'modified submodule contains untracked and modifed content (all ignored)' '
	echo modification >> sm1/foo6 &&
	git diff-index -p --ignore-submodules --submodule=log HEAD >actual &&
	! test -s actual
'

test_expect_success 'modified submodule contains modifed content' '
	rm -f sm1/new-file &&
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 contains modified content
	Submodule sm1 $head6..$head8:
	  > change
	EOF
	test_cmp expected actual
'

rm -rf sm1
test_expect_success 'deleted submodule' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	EOF
	test_cmp expected actual
'

test_create_repo sm2 &&
head7=$(add_file sm2 foo8 foo9) &&
git add sm2

test_expect_success 'multiple submodules' '
	git diff-index -p --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'path filter' '
	git diff-index -p --submodule=log HEAD sm2 >actual &&
	cat >expected <<-EOF &&
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

commit_file sm2
test_expect_success 'given commit' '
	git diff-index -p --submodule=log HEAD^ >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'given commit --submodule' '
	git diff-index -p --submodule HEAD^ >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

fullhead7=$(cd sm2; git rev-parse --verify HEAD)

test_expect_success 'given commit --submodule=short' '
	git diff-index -p --submodule=short HEAD^ >actual &&
	cat >expected <<-EOF &&
	diff --git a/sm1 b/sm1
	deleted file mode 160000
	index $head6..0000000
	--- a/sm1
	+++ /dev/null
	@@ -1 +0,0 @@
	-Subproject commit $fullhead6
	diff --git a/sm2 b/sm2
	new file mode 160000
	index 0000000..$head7
	--- /dev/null
	+++ b/sm2
	@@ -0,0 +1 @@
	+Subproject commit $fullhead7
	EOF
	test_cmp expected actual
'

test_expect_success 'setup .git file for sm2' '
	(cd sm2 &&
	 REAL="$(pwd)/../.real" &&
	 mv .git "$REAL"
	 echo "gitdir: $REAL" >.git)
'

test_expect_success 'diff --submodule with .git file' '
	git diff --submodule HEAD^ >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $head6...0000000 (submodule deleted)
	Submodule sm2 0000000...$head7 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'diff --submodule with objects referenced by alternates' '
	mkdir sub_alt &&
	(cd sub_alt &&
		git init &&
		echo a >a &&
		git add a &&
		git commit -m a
	) &&
	mkdir super &&
	(cd super &&
		git clone -s ../sub_alt sub &&
		git init &&
		git add sub &&
		git commit -m "sub a"
	) &&
	(cd sub_alt &&
		sha1_before=$(git rev-parse --short HEAD)
		echo b >b &&
		git add b &&
		git commit -m b
		sha1_after=$(git rev-parse --short HEAD)
		echo "Submodule sub $sha1_before..$sha1_after:
  > b" >../expected
	) &&
	(cd super &&
		(cd sub &&
			git fetch &&
			git checkout origin/master
		) &&
		git diff --submodule > ../actual
	)
	test_cmp expected actual
'

test_done
