#!/bin/sh
#
# Copyright (c) 2010 Will Palmer
#

test_description='git merge-tree'
. ./test-lib.sh

test_expect_success setup '
	test_commit "initial" "initial-file" "initial"
'

test_expect_success 'file add A, !B' '
	cat >expected <<\EXPECTED &&
added in remote
  their  100644 43d5a8ed6ef6c00ff775008633f95787d088285d ONE
@@ -0,0 +1 @@
+AAA
EXPECTED

	git reset --hard initial &&
	test_commit "add-a-not-b" "ONE" "AAA" &&
	git merge-tree initial initial add-a-not-b >actual &&
	test_cmp expected actual
'

test_expect_success 'file add !A, B' '
	git reset --hard initial &&
	test_commit "add-not-a-b" "ONE" "AAA" &&
	git merge-tree initial add-not-a-b initial >actual &&
	test_must_be_empty actual
'

test_expect_success 'file add A, B (same)' '
	git reset --hard initial &&
	test_commit "add-a-b-same-A" "ONE" "AAA" &&
	git reset --hard initial &&
	test_commit "add-a-b-same-B" "ONE" "AAA" &&
	git merge-tree initial add-a-b-same-A add-a-b-same-B >actual &&
	test_must_be_empty actual
'

test_expect_success 'file add A, B (different)' '
	cat >expected <<\EXPECTED &&
added in both
  our    100644 43d5a8ed6ef6c00ff775008633f95787d088285d ONE
  their  100644 ba629238ca89489f2b350e196ca445e09d8bb834 ONE
@@ -1 +1,5 @@
+<<<<<<< .our
 AAA
+=======
+BBB
+>>>>>>> .their
EXPECTED

	git reset --hard initial &&
	test_commit "add-a-b-diff-A" "ONE" "AAA" &&
	git reset --hard initial &&
	test_commit "add-a-b-diff-B" "ONE" "BBB" &&
	git merge-tree initial add-a-b-diff-A add-a-b-diff-B >actual &&
	test_cmp expected actual
'

test_expect_success 'file change A, !B' '
	git reset --hard initial &&
	test_commit "change-a-not-b" "initial-file" "BBB" &&
	git merge-tree initial change-a-not-b initial >actual &&
	test_must_be_empty actual
'

test_expect_success 'file change !A, B' '
	cat >expected <<\EXPECTED &&
merged
  result 100644 ba629238ca89489f2b350e196ca445e09d8bb834 initial-file
  our    100644 e79c5e8f964493290a409888d5413a737e8e5dd5 initial-file
@@ -1 +1 @@
-initial
+BBB
EXPECTED

	git reset --hard initial &&
	test_commit "change-not-a-b" "initial-file" "BBB" &&
	git merge-tree initial initial change-not-a-b >actual &&
	test_cmp expected actual
'

test_expect_success 'file change A, B (same)' '
	git reset --hard initial &&
	test_commit "change-a-b-same-A" "initial-file" "AAA" &&
	git reset --hard initial &&
	test_commit "change-a-b-same-B" "initial-file" "AAA" &&
	git merge-tree initial change-a-b-same-A change-a-b-same-B >actual &&
	test_must_be_empty actual
'

test_expect_success 'file change A, B (different)' '
	cat >expected <<\EXPECTED &&
changed in both
  base   100644 e79c5e8f964493290a409888d5413a737e8e5dd5 initial-file
  our    100644 43d5a8ed6ef6c00ff775008633f95787d088285d initial-file
  their  100644 ba629238ca89489f2b350e196ca445e09d8bb834 initial-file
@@ -1 +1,5 @@
+<<<<<<< .our
 AAA
+=======
+BBB
+>>>>>>> .their
EXPECTED

	git reset --hard initial &&
	test_commit "change-a-b-diff-A" "initial-file" "AAA" &&
	git reset --hard initial &&
	test_commit "change-a-b-diff-B" "initial-file" "BBB" &&
	git merge-tree initial change-a-b-diff-A change-a-b-diff-B >actual &&
	test_cmp expected actual
'

test_expect_success 'file change A, B (mixed)' '
	cat >expected <<\EXPECTED &&
changed in both
  base   100644 f4f1f998c7776568c4ff38f516d77fef9399b5a7 ONE
  our    100644 af14c2c3475337c73759d561ef70b59e5c731176 ONE
  their  100644 372d761493f524d44d59bd24700c3bdf914c973c ONE
@@ -7,7 +7,11 @@
 AAA
 AAA
 AAA
+<<<<<<< .our
 BBB
+=======
+CCC
+>>>>>>> .their
 AAA
 AAA
 AAA
EXPECTED

	git reset --hard initial &&
	test_commit "change-a-b-mix-base" "ONE" "
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA
AAA" &&
	test_commit "change-a-b-mix-A" "ONE" \
		"$(sed -e "1{s/AAA/BBB/;}" -e "10{s/AAA/BBB/;}" <ONE)" &&
	git reset --hard change-a-b-mix-base &&
	test_commit "change-a-b-mix-B" "ONE" \
		"$(sed -e "1{s/AAA/BBB/;}" -e "10{s/AAA/CCC/;}" <ONE)" &&
	git merge-tree change-a-b-mix-base change-a-b-mix-A change-a-b-mix-B \
		>actual &&
	test_cmp expected actual
'

test_expect_success 'file remove A, !B' '
	git reset --hard initial &&
	test_commit "rm-a-not-b-base" "ONE" "AAA" &&
	git rm ONE &&
	git commit -m "rm-a-not-b" &&
	git tag "rm-a-not-b" &&
	git merge-tree rm-a-not-b-base rm-a-not-b rm-a-not-b-base >actual &&
	test_must_be_empty actual
'

test_expect_success 'file remove !A, B' '
	cat >expected <<\EXPECTED &&
removed in remote
  base   100644 43d5a8ed6ef6c00ff775008633f95787d088285d ONE
  our    100644 43d5a8ed6ef6c00ff775008633f95787d088285d ONE
@@ -1 +0,0 @@
-AAA
EXPECTED

	git reset --hard initial &&
	test_commit "rm-not-a-b-base" "ONE" "AAA" &&
	git rm ONE &&
	git commit -m "rm-not-a-b" &&
	git tag "rm-not-a-b" &&
	git merge-tree rm-a-not-b-base rm-a-not-b-base rm-a-not-b >actual &&
	test_cmp expected actual
'

test_expect_success 'file remove A, B (same)' '
	git reset --hard initial &&
	test_commit "rm-a-b-base" "ONE" "AAA" &&
	git rm ONE &&
	git commit -m "rm-a-b" &&
	git tag "rm-a-b" &&
	git merge-tree rm-a-b-base rm-a-b rm-a-b >actual &&
	test_must_be_empty actual
'

test_expect_success 'file change A, remove B' '
	cat >expected <<\EXPECTED &&
removed in remote
  base   100644 43d5a8ed6ef6c00ff775008633f95787d088285d ONE
  our    100644 ba629238ca89489f2b350e196ca445e09d8bb834 ONE
@@ -1 +0,0 @@
-BBB
EXPECTED

	git reset --hard initial &&
	test_commit "change-a-rm-b-base" "ONE" "AAA" &&
	test_commit "change-a-rm-b-A" "ONE" "BBB" &&
	git reset --hard change-a-rm-b-base &&
	git rm ONE &&
	git commit -m "change-a-rm-b-B" &&
	git tag "change-a-rm-b-B" &&
	git merge-tree change-a-rm-b-base change-a-rm-b-A change-a-rm-b-B \
		>actual &&
	test_cmp expected actual
'

test_expect_success 'file remove A, change B' '
	cat >expected <<\EXPECTED &&
removed in local
  base   100644 43d5a8ed6ef6c00ff775008633f95787d088285d ONE
  their  100644 ba629238ca89489f2b350e196ca445e09d8bb834 ONE
EXPECTED

	git reset --hard initial &&
	test_commit "rm-a-change-b-base" "ONE" "AAA" &&

	git rm ONE &&
	git commit -m "rm-a-change-b-A" &&
	git tag "rm-a-change-b-A" &&
	git reset --hard rm-a-change-b-base &&
	test_commit "rm-a-change-b-B" "ONE" "BBB" &&
	git merge-tree rm-a-change-b-base rm-a-change-b-A rm-a-change-b-B \
		>actual &&
	test_cmp expected actual
'

test_expect_success 'tree add A, B (same)' '
	git reset --hard initial &&
	mkdir sub &&
	test_commit "add sub/file" "sub/file" "file" add-tree-A &&
	git merge-tree initial add-tree-A add-tree-A >actual &&
	test_must_be_empty actual
'

test_expect_success 'tree add A, B (different)' '
	cat >expect <<-\EOF &&
	added in both
	  our    100644 43d5a8ed6ef6c00ff775008633f95787d088285d sub/file
	  their  100644 ba629238ca89489f2b350e196ca445e09d8bb834 sub/file
	@@ -1 +1,5 @@
	+<<<<<<< .our
	 AAA
	+=======
	+BBB
	+>>>>>>> .their
	EOF
	git reset --hard initial &&
	mkdir sub &&
	test_commit "add sub/file" "sub/file" "AAA" add-tree-a-b-A &&
	git reset --hard initial &&
	mkdir sub &&
	test_commit "add sub/file" "sub/file" "BBB" add-tree-a-b-B &&
	git merge-tree initial add-tree-a-b-A add-tree-a-b-B >actual &&
	test_cmp expect actual
'

test_expect_success 'tree unchanged A, removed B' '
	cat >expect <<-\EOF &&
	removed in remote
	  base   100644 43d5a8ed6ef6c00ff775008633f95787d088285d sub/file
	  our    100644 43d5a8ed6ef6c00ff775008633f95787d088285d sub/file
	@@ -1 +0,0 @@
	-AAA
	EOF
	git reset --hard initial &&
	mkdir sub &&
	test_commit "add sub/file" "sub/file" "AAA" tree-remove-b-initial &&
	git rm sub/file &&
	test_tick &&
	git commit -m "remove sub/file" &&
	git tag tree-remove-b-B &&
	git merge-tree tree-remove-b-initial tree-remove-b-initial tree-remove-b-B >actual &&
	test_cmp expect actual
'

test_expect_success 'turn file to tree' '
	git reset --hard initial &&
	rm initial-file &&
	mkdir initial-file &&
	test_commit "turn-file-to-tree" "initial-file/ONE" "CCC" &&
	git merge-tree initial initial turn-file-to-tree >actual &&
	cat >expect <<-\EOF &&
	added in remote
	  their  100644 43aa4fdec31eb92e1fdc2f0ce6ea9ddb7c32bcf7 initial-file/ONE
	@@ -0,0 +1 @@
	+CCC
	removed in remote
	  base   100644 e79c5e8f964493290a409888d5413a737e8e5dd5 initial-file
	  our    100644 e79c5e8f964493290a409888d5413a737e8e5dd5 initial-file
	@@ -1 +0,0 @@
	-initial
	EOF
	test_cmp expect actual
'

test_expect_success 'turn tree to file' '
	git reset --hard initial &&
	mkdir dir &&
	test_commit "add-tree" "dir/path" "AAA" &&
	test_commit "add-another-tree" "dir/another" "BBB" &&
	rm -fr dir &&
	test_commit "make-file" "dir" "CCC" &&
	git merge-tree add-tree add-another-tree make-file >actual &&
	cat >expect <<-\EOF &&
	removed in remote
	  base   100644 43d5a8ed6ef6c00ff775008633f95787d088285d dir/path
	  our    100644 43d5a8ed6ef6c00ff775008633f95787d088285d dir/path
	@@ -1 +0,0 @@
	-AAA
	added in remote
	  their  100644 43aa4fdec31eb92e1fdc2f0ce6ea9ddb7c32bcf7 dir
	@@ -0,0 +1 @@
	+CCC
	EOF
	test_cmp expect actual
'

test_done
