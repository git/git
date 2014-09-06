#!/bin/sh

test_description='test log --remerge-diff'
. ./test-lib.sh

# A -----------------+----
# | \  \      \      |    \
# |  C  \      \     |    |
# B  |\  \      |    |    |
# |  | |  D     U   dir  file
# |\ | |__|__   |    | \ /|
# | X  |_ |  \  |    |  X |
# |/ \/  \|   \ |    | / \|
# M1 M2   M3   M4    M5   M6
# ^  ^    ^     ^    ^    ^
# |  |    |     |    |    filedir
# |  |    |     |    dirfile
# |  |    dm    unrelated
# |  evil
# benign
#
#
# M1 has a "benign" conflict
# M2 has an "evil" conflict: it ignores the changes in D
# M3 has a delete/modify conflict, resolved in favor of a modification
# M4 is a merge of an unrelated change, without conflicts
# M5 has a file/directory conflict, resolved in favor of the directory
# M6 has a file/directory conflict, resolved in favor of the file

test_expect_success 'setup' '
	test_commit A file original &&
	test_commit B file change &&
	git checkout -b side A &&
	test_commit C file side &&
	git checkout -b delete A &&
	git rm file &&
	test_commit D &&
	git checkout -b benign master &&
	test_must_fail git merge C &&
	test_commit M1 file merged &&
	git checkout -b evil B &&
	test_must_fail git merge C &&
	test_commit M2 file change &&
	git checkout -b dm C &&
	test_must_fail git merge D &&
	test_commit M3 file resolved &&
	git checkout -b unrelated A &&
	test_commit unrelated_file &&
	git merge C &&
	test_tick &&
	git tag M4 &&
	git checkout -b dir A &&
	mkdir sub &&
	test_commit dir sub/file &&
	git checkout -b file A &&
	test_commit file sub &&
	git checkout -b dirfile tags/dir &&
	test_must_fail git merge tags/file &&
	git rm --cached sub &&
	test_commit M5 sub/file resolved &&
	git checkout -b filedir tags/file &&
	test_must_fail git merge tags/dir &&
	git rm --cached sub/file &&
	rm -rf sub &&
	test_commit M6 sub resolved &&
	git branch -D master side delete dir file
'

test_expect_success 'unrelated merge: without conflicts' '
	git log -p --cc unrelated >expected &&
	git log -p --remerge-diff unrelated >actual &&
	test_cmp expected actual
'

clean_output () {
	git name-rev --name-only --stdin |
	# strip away bits that aren't treated by the above
	sed -e 's/^\(index\|Merge:\|Date:\).*/\1/'
}

cat >expected <<EOF
commit benign
Merge:
Author: A U Thor <author@example.com>
Date:

    M1

diff --git a/file b/file
index
--- a/file
+++ b/file
@@ -1,5 +1 @@
-<<<<<<< tags/B
-change
-=======
-side
->>>>>>> tags/C
+merged
EOF

test_expect_success 'benign merge: conflicts resolved' '
	git log -1 -p --remerge-diff benign >output &&
	clean_output <output >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
commit evil
Merge:
Author: A U Thor <author@example.com>
Date:

    M2

diff --git a/file b/file
index
--- a/file
+++ b/file
@@ -1,5 +1 @@
-<<<<<<< tags/B
 change
-=======
-side
->>>>>>> tags/C
EOF

test_expect_success 'evil merge: changes ignored' '
	git log -1 --remerge-diff -p evil >output &&
	clean_output <output >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
commit dm
Merge:
Author: A U Thor <author@example.com>
Date:

    M3

diff --git a/file b/file
index
--- a/file
+++ b/file
@@ -1,4 +1 @@
-<<<<<<< tags/C
-side
-=======
->>>>>>> tags/D
+resolved
EOF

test_expect_success 'delete/modify conflict' '
	git log -1 --remerge-diff -p dm >output &&
	clean_output <output >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
commit dirfile
Merge:
Author: A U Thor <author@example.com>
Date:

    M5

diff --git a/sub/file b/sub/file
index
--- a/sub/file
+++ b/sub/file
@@ -1 +1 @@
-dir
+resolved
diff --git a/sub~tags/file b/sub~tags/file
deleted file mode 100644
index
--- a/sub~tags/file
+++ /dev/null
@@ -1 +0,0 @@
-file
EOF

test_expect_success 'file/directory conflict resulting in directory' '
	git log -1 --remerge-diff -p dirfile >output &&
	clean_output <output >actual &&
	test_cmp expected actual
'

# This is wishful thinking, see the NEEDSWORK in
# make_asymmetric_conflict_entries().
cat >expected <<EOF
commit filedir
Merge:
Author: A U Thor <author@example.com>
Date:

    M6

diff --git a/sub b/sub
index
--- a/sub
+++ b/sub
@@ -1 +1 @@
-file
+resolved
diff --git a/sub/file b/sub/file
deleted file mode 100644
index
--- a/sub/file
+++ /dev/null
@@ -1 +0,0 @@
-dir
EOF

test_expect_failure 'file/directory conflict resulting in file' '
	git log -1 --remerge-diff -p filedir >output &&
	clean_output <output >actual &&
	test_cmp expected actual
'

test_done
