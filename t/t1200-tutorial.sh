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
	test_cmp diff.expect diff.output
'

test_expect_success 'git diff' '
	git diff > diff.output &&
	test_cmp diff.expect diff.output
'

test_expect_success 'tree' '
	tree=$(git write-tree 2>/dev/null) &&
	test 8988da15d077d4829fc51d8544c097def6644dbb = $tree
'

test_expect_success 'git diff-index -p HEAD' '
	test_tick &&
	tree=$(git write-tree) &&
	commit=$(echo "Initial commit" | git commit-tree $tree) &&
	git update-ref HEAD $commit &&
	git diff-index -p HEAD > diff.output &&
	test_cmp diff.expect diff.output
'

test_expect_success 'git diff HEAD' '
	git diff HEAD > diff.output &&
	test_cmp diff.expect diff.output
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
	git whatchanged -p --root |
		sed -e "1s/^\(.\{7\}\).\{40\}/\1VARIABLE/" \
		-e "2,3s/^\(.\{8\}\).*$/\1VARIABLE/" \
	> whatchanged.output &&
	test_cmp whatchanged.expect whatchanged.output
'

test_expect_success 'git tag my-first-tag' '
	git tag my-first-tag &&
	test_cmp .git/refs/heads/master .git/refs/tags/my-first-tag
'

test_expect_success 'git checkout -b mybranch' '
	git checkout -b mybranch &&
	test_cmp .git/refs/heads/master .git/refs/heads/mybranch
'

cat > branch.expect <<EOF
  master
* mybranch
EOF

test_expect_success 'git branch' '
	git branch > branch.output &&
	test_cmp branch.expect branch.output
'

test_expect_success 'git resolve now fails' '
	git checkout mybranch &&
	echo "Work, work, work" >>hello &&
	test_tick &&
	git commit -m "Some work." -i hello &&

	git checkout master &&

	echo "Play, play, play" >>hello &&
	echo "Lots of fun" >>example &&
	test_tick &&
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
* [master] Merge work in mybranch
 ! [mybranch] Some work.
--
-  [master] Merge work in mybranch
*+ [mybranch] Some work.
*  [master^] Some fun.
EOF

test_expect_success 'git show-branch' '
	test_tick &&
	git commit -m "Merge work in mybranch" -i hello &&
	git show-branch --topo-order --more=1 master mybranch \
		> show-branch.output &&
	test_cmp show-branch.expect show-branch.output
'

cat > resolve.expect << EOF
Updating VARIABLE..VARIABLE
FASTFORWARD (no commit created; -m option ignored)
 example | 1 +
 hello   | 1 +
 2 files changed, 2 insertions(+)
EOF

test_expect_success 'git resolve' '
	git checkout mybranch &&
	git merge -m "Merge upstream changes." master |
		sed -e "1s/[0-9a-f]\{7\}/VARIABLE/g" \
		-e "s/^Fast[- ]forward /FASTFORWARD /" >resolve.output
'

test_expect_success 'git resolve output' '
	test_i18ncmp resolve.expect resolve.output
'

cat > show-branch2.expect << EOF
! [master] Merge work in mybranch
 * [mybranch] Merge work in mybranch
--
-- [master] Merge work in mybranch
EOF

test_expect_success 'git show-branch (part 2)' '
	git show-branch --topo-order master mybranch > show-branch2.output &&
	test_cmp show-branch2.expect show-branch2.output
'

cat > show-branch3.expect << EOF
! [master] Merge work in mybranch
 * [mybranch] Merge work in mybranch
--
-- [master] Merge work in mybranch
+* [master^2] Some work.
+* [master^] Some fun.
EOF

test_expect_success 'git show-branch (part 3)' '
	git show-branch --topo-order --more=2 master mybranch \
		> show-branch3.output &&
	test_cmp show-branch3.expect show-branch3.output
'

test_expect_success 'rewind to "Some fun." and "Some work."' '
	git checkout mybranch &&
	git reset --hard master^2 &&
	git checkout master &&
	git reset --hard master^
'

cat > show-branch4.expect << EOF
* [master] Some fun.
 ! [mybranch] Some work.
--
*  [master] Some fun.
 + [mybranch] Some work.
*+ [master^] Initial commit
EOF

test_expect_success 'git show-branch (part 4)' '
	git show-branch --topo-order > show-branch4.output &&
	test_cmp show-branch4.expect show-branch4.output
'

test_expect_success 'manual merge' '
	mb=$(git merge-base HEAD mybranch) &&
	git name-rev --name-only --tags $mb > name-rev.output &&
	test "my-first-tag" = $(cat name-rev.output) &&

	git read-tree -m -u $mb HEAD mybranch
'

cat > ls-files.expect << EOF
100644 7f8b141b65fdcee47321e399a2598a235a032422 0	example
100644 557db03de997c86a4a028e1ebd3a1ceb225be238 1	hello
100644 ba42a2a96e3027f3333e13ede4ccf4498c3ae942 2	hello
100644 cc44c73eb783565da5831b4d820c962954019b69 3	hello
EOF

test_expect_success 'git ls-files --stage' '
	git ls-files --stage > ls-files.output &&
	test_cmp ls-files.expect ls-files.output
'

cat > ls-files-unmerged.expect << EOF
100644 557db03de997c86a4a028e1ebd3a1ceb225be238 1	hello
100644 ba42a2a96e3027f3333e13ede4ccf4498c3ae942 2	hello
100644 cc44c73eb783565da5831b4d820c962954019b69 3	hello
EOF

test_expect_success 'git ls-files --unmerged' '
	git ls-files --unmerged > ls-files-unmerged.output &&
	test_cmp ls-files-unmerged.expect ls-files-unmerged.output
'

test_expect_success 'git-merge-index' '
	test_must_fail git merge-index git-merge-one-file hello
'

test_expect_success 'git ls-files --stage (part 2)' '
	git ls-files --stage > ls-files.output2 &&
	test_cmp ls-files.expect ls-files.output2
'

test_expect_success 'git repack' 'git repack'
test_expect_success 'git prune-packed' 'git prune-packed'
test_expect_success '-> only packed objects' '
	git prune && # Remove conflict marked blobs
	test $(find .git/objects/[0-9a-f][0-9a-f] -type f -print 2>/dev/null | wc -l) = 0
'

test_done
