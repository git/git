#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='A simple turial in the form of a test case'

. ./test-lib.sh

echo "Hello World" > hello
echo "Silly example" > example

git-update-index --add hello example

test_expect_success 'blob' "test blob = \"$(git-cat-file -t 557db03)\""

test_expect_success 'blob 557db03' "test \"Hello World\" = \"$(git-cat-file blob 557db03)\""

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
git-diff-files -p > diff.output
test_expect_success 'git-diff-files -p' 'cmp diff.expect diff.output'
git diff > diff.output
test_expect_success 'git diff' 'cmp diff.expect diff.output'

tree=$(git-write-tree 2>/dev/null)

test_expect_success 'tree' "test 8988da15d077d4829fc51d8544c097def6644dbb = $tree"

output="$(echo "Initial commit" | git-commit-tree $(git-write-tree) 2>&1 > .git/refs/heads/master)"

git-diff-index -p HEAD > diff.output
test_expect_success 'git-diff-index -p HEAD' 'cmp diff.expect diff.output'

git diff HEAD > diff.output
test_expect_success 'git diff HEAD' 'cmp diff.expect diff.output'

#rm hello
#test_expect_success 'git-read-tree --reset HEAD' "git-read-tree --reset HEAD ; test \"hello: needs update\" = \"$(git-update-index --refresh)\""

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

git-whatchanged -p --root | \
	sed -e "1s/^\(.\{7\}\).\{40\}/\1VARIABLE/" \
		-e "2,3s/^\(.\{8\}\).*$/\1VARIABLE/" \
> whatchanged.output
test_expect_success 'git-whatchanged -p --root' 'cmp whatchanged.expect whatchanged.output'

git tag my-first-tag
test_expect_success 'git tag my-first-tag' 'cmp .git/refs/heads/master .git/refs/tags/my-first-tag'

# TODO: test git-clone

git checkout -b mybranch
test_expect_success 'git checkout -b mybranch' 'cmp .git/refs/heads/master .git/refs/heads/mybranch'

cat > branch.expect <<EOF
  master
* mybranch
EOF

git branch > branch.output
test_expect_success 'git branch' 'cmp branch.expect branch.output'

git checkout mybranch
echo "Work, work, work" >>hello
git commit -m 'Some work.' -i hello

git checkout master

echo "Play, play, play" >>hello
echo "Lots of fun" >>example
git commit -m 'Some fun.' -i hello example

test_expect_failure 'git resolve now fails' '
	git merge -m "Merge work in mybranch" mybranch
'

cat > hello << EOF
Hello World
It's a new day for git
Play, play, play
Work, work, work
EOF

git commit -m 'Merged "mybranch" changes.' -i hello

test_done

cat > show-branch.expect << EOF
* [master] Merged "mybranch" changes.
 ! [mybranch] Some work.
--
-  [master] Merged "mybranch" changes.
*+ [mybranch] Some work.
EOF

git show-branch --topo-order master mybranch > show-branch.output
test_expect_success 'git show-branch' 'cmp show-branch.expect show-branch.output'

git checkout mybranch

cat > resolve.expect << EOF
Updating from VARIABLE to VARIABLE
 example |    1 +
 hello   |    1 +
 2 files changed, 2 insertions(+), 0 deletions(-)
EOF

git merge -s "Merge upstream changes." master | \
	sed -e "1s/[0-9a-f]\{40\}/VARIABLE/g" >resolve.output
test_expect_success 'git resolve' 'cmp resolve.expect resolve.output'

cat > show-branch2.expect << EOF
! [master] Merged "mybranch" changes.
 * [mybranch] Merged "mybranch" changes.
--
-- [master] Merged "mybranch" changes.
EOF

git show-branch --topo-order master mybranch > show-branch2.output
test_expect_success 'git show-branch' 'cmp show-branch2.expect show-branch2.output'

# TODO: test git fetch

# TODO: test git push

test_expect_success 'git repack' 'git repack'
test_expect_success 'git prune-packed' 'git prune-packed'
test_expect_failure '-> only packed objects' 'find -type f .git/objects/[0-9a-f][0-9a-f]'

test_done

