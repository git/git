#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='A simple turial in the form of a test case'

. ./test-lib.sh

test_expect_success 'blob'  '
	echo "Hello World" > hello &&
	echo "Silly example" > example &&

	git update-index --add hello example &&

	test blob = "$(git cat-file -t 557db03)"
'

test_expect_success 'blob 557db03' '
	test "Hello World" = "$(git cat-file blob 557db03)"
'

echo "It's a new day for git" >>hello
cat > diff.expect << EOF
diff --git a/hello b/hello
index 557db03..263414f 100644
--- a/hello
+++ b/hello
@@ -1 +1,2 @@
 Hello World
+It's a new day for git
EOF

test_expect_success 'git diff-files -p' '
	git diff-files -p > diff.output &&
	cmp diff.expect diff.output
'

test_expect_success 'git diff' '
	git diff > diff.output &&
	cmp diff.expect diff.output
'

test_expect_success 'tree' '
	tree=$(git write-tree 2>/dev/null)
	test 8988da15d077d4829fc51d8544c097def6644dbb = $tree
'

test_expect_success 'git diff-index -p HEAD' '
	echo "Initial commit" | \
	git commit-tree $(git write-tree) 2>&1 > .git/refs/heads/master &&
	git diff-index -p HEAD > diff.output &&
	cmp diff.expect diff.output
'

test_expect_success 'git diff HEAD' '
	git diff HEAD > diff.output &&
	cmp diff.expect diff.output
'

cat > whatchanged.expect << EOF
commit VARIABLE
Author: VARIABLE
Date:   VARIABLE

    Initial commit

diff --git a/example b/example
new file mode 100644
index 0000000..f24c74a
--- /dev/null
+++ b/example
@@ -0,0 +1 @@
+Silly example
diff --git a/hello b/hello
new file mode 100644
index 0000000..557db03
--- /dev/null
+++ b/hello
@@ -0,0 +1 @@
+Hello World
EOF

test_expect_success 'git whatchanged -p --root' '
	git whatchanged -p --root | \
		sed -e "1s/^\(.\{7\}\).\{40\}/\1VARIABLE/" \
		-e "2,3s/^\(.\{8\}\).*$/\1VARIABLE/" \
	> whatchanged.output &&
	cmp whatchanged.expect whatchanged.output
'

test_expect_success 'git tag my-first-tag' '
	git tag my-first-tag &&
	cmp .git/refs/heads/master .git/refs/tags/my-first-tag
'

test_expect_success 'git checkout -b mybranch' '
	git checkout -b mybranch &&
	cmp .git/refs/heads/master .git/refs/heads/mybranch
'

cat > branch.expect <<EOF
  master
* mybranch
EOF

test_expect_success 'git branch' '
	git branch > branch.output &&
	cmp branch.expect branch.output
'

test_expect_success 'git resolve now fails' '
	git checkout mybranch &&
	echo "Work, work, work" >>hello &&
	git commit -m "Some work." -i hello &&

	git checkout master &&

	echo "Play, play, play" >>hello &&
	echo "Lots of fun" >>example &&
	git commit -m "Some fun." -i hello example &&

	test_must_fail git merge -m "Merge work in mybranch" mybranch
'

cat > hello << EOF
Hello World
It's a new day for git
Play, play, play
Work, work, work
EOF

cat > show-branch.expect << EOF
* [master] Merged "mybranch" changes.
 ! [mybranch] Some work.
--
-  [master] Merged "mybranch" changes.
*+ [mybranch] Some work.
EOF

test_expect_success 'git show-branch' '
	git commit -m "Merged \"mybranch\" changes." -i hello &&
	git show-branch --topo-order master mybranch > show-branch.output &&
	cmp show-branch.expect show-branch.output
'

cat > resolve.expect << EOF
Updating VARIABLE..VARIABLE
Fast forward (no commit created; -m option ignored)
 example |    1 +
 hello   |    1 +
 2 files changed, 2 insertions(+), 0 deletions(-)
EOF

test_expect_success 'git resolve' '
	git checkout mybranch &&
	git merge -m "Merge upstream changes." master | \
		sed -e "1s/[0-9a-f]\{7\}/VARIABLE/g" >resolve.output &&
	cmp resolve.expect resolve.output
'

cat > show-branch2.expect << EOF
! [master] Merged "mybranch" changes.
 * [mybranch] Merged "mybranch" changes.
--
-- [master] Merged "mybranch" changes.
EOF

test_expect_success 'git show-branch (part 2)' '
	git show-branch --topo-order master mybranch > show-branch2.output &&
	cmp show-branch2.expect show-branch2.output
'

test_expect_success 'git repack' 'git repack'
test_expect_success 'git prune-packed' 'git prune-packed'
test_expect_success '-> only packed objects' '
	git prune && # Remove conflict marked blobs
	! find .git/objects/[0-9a-f][0-9a-f] -type f
'

test_done
