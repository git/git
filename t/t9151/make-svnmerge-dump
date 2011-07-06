#!/bin/sh
#
# this script sets up a Subversion repository for Makefile in the
# first ever git merge, as if it were done with svnmerge (SVN 1.5+)
#

rm -rf foo.svn foo
set -e

mkdir foo.svn
svnadmin create foo.svn
svn co file://`pwd`/foo.svn foo

commit() {
    i=$(( $1 + 1 ))
    shift;
    svn commit -m "(r$i) $*" >/dev/null || exit 1
    echo $i
}

say() {
    echo "[1m * $*[0m"
}

i=0
cd foo
mkdir trunk
mkdir branches
mkdir tags
svn add trunk branches tags
i=$(commit $i "Setup trunk, branches, and tags")

git cat-file blob 6683463e:Makefile > trunk/Makefile
svn add trunk/Makefile 

say "Committing ANCESTOR"
i=$(commit $i "ancestor")
svn cp trunk branches/left

say "Committing BRANCH POINT"
i=$(commit $i "make left branch")
svn cp trunk branches/right

say "Committing other BRANCH POINT"
i=$(commit $i "make right branch")

say "Committing LEFT UPDATE"
git cat-file blob 5873b67e:Makefile > branches/left/Makefile
i=$(commit $i "left update 1")

git cat-file blob 75118b13:Makefile > branches/right/Makefile
say "Committing RIGHT UPDATE"
pre_right_update_1=$i
i=$(commit $i "right update 1")

say "Making more commits on LEFT"
git cat-file blob ff5ebe39:Makefile > branches/left/Makefile
i=$(commit $i "left update 2")
git cat-file blob b5039db6:Makefile > branches/left/Makefile
i=$(commit $i "left update 3")

say "Making a LEFT SUB-BRANCH"
svn cp branches/left branches/left-sub
sub_left_make=$i
i=$(commit $i "make left sub-branch")

say "Making a commit on LEFT SUB-BRANCH"
echo "crunch" > branches/left-sub/README
svn add branches/left-sub/README
i=$(commit $i "left sub-branch update 1")

say "Merging LEFT to TRUNK"
svn update
cd trunk
svn merge ../branches/left --accept postpone
git cat-file blob b5039db6:Makefile > Makefile
svn resolved Makefile
i=$(commit $i "Merge left to trunk 1")
cd ..

say "Making more commits on LEFT and RIGHT"
echo "touche" > branches/left/zlonk
svn add branches/left/zlonk
i=$(commit $i "left update 4")
echo "thwacke" > branches/right/bang
svn add branches/right/bang
i=$(commit $i "right update 2")

say "Squash merge of RIGHT tip 2 commits onto TRUNK"
svn update
cd trunk
svn merge -r$pre_right_update_1:$i ../branches/right
i=$(commit $i "Cherry-pick right 2 commits to trunk")
cd ..

say "Merging RIGHT to TRUNK"
svn update
cd trunk
svn merge ../branches/right --accept postpone
git cat-file blob b51ad431:Makefile > Makefile
svn resolved Makefile
i=$(commit $i "Merge right to trunk 1")
cd ..

say "Making more commits on RIGHT and TRUNK"
echo "whamm" > branches/right/urkkk
svn add branches/right/urkkk
i=$(commit $i "right update 3")
echo "pow" > trunk/vronk
svn add trunk/vronk
i=$(commit $i "trunk update 1")

say "Merging RIGHT to LEFT SUB-BRANCH"
svn update
cd branches/left-sub
svn merge ../right --accept postpone
git cat-file blob b51ad431:Makefile > Makefile
svn resolved Makefile
i=$(commit $i "Merge right to left sub-branch")
cd ../..

say "Making more commits on LEFT SUB-BRANCH and LEFT"
echo "zowie" > branches/left-sub/wham_eth
svn add branches/left-sub/wham_eth
pre_sub_left_update_2=$i
i=$(commit $i "left sub-branch update 2")
sub_left_update_2=$i
echo "eee_yow" > branches/left/glurpp
svn add branches/left/glurpp
i=$(commit $i "left update 5")

say "Cherry pick LEFT SUB-BRANCH commit to LEFT"
svn update
cd branches/left
svn merge -r$pre_sub_left_update_2:$sub_left_update_2 ../left-sub
i=$(commit $i "Cherry-pick left sub-branch commit to left")
cd ../..

say "Merging LEFT SUB-BRANCH back to LEFT"
svn update
cd branches/left
# it's only a merge because the previous merge cherry-picked the top commit
svn merge -r$sub_left_make:$sub_left_update_2 ../left-sub --accept postpone
i=$(commit $i "Merge left sub-branch to left")
cd ../..

say "Merging EVERYTHING to TRUNK"
svn update
cd trunk
svn merge ../branches/left --accept postpone
svn resolved bang
i=$(commit $i "Merge left to trunk 2")
# this merge, svn happily updates the mergeinfo, but there is actually
# nothing to merge.  git-svn will not make a meaningless merge commit.
svn merge ../branches/right --accept postpone
i=$(commit $i "non-merge right to trunk 2")
cd ..

say "Branching b1 from trunk"
svn update
svn cp trunk branches/b1
i=$(commit $i "make b1 branch from trunk")

say "Branching b2 from trunk"
svn update
svn cp trunk branches/b2
i=$(commit $i "make b2 branch from trunk")

say "Make a commit to b2"
svn update
cd branches/b2
echo "b2" > b2file
svn add b2file
i=$(commit $i "b2 update 1")
cd ../..

say "Make a commit to b1"
svn update
cd branches/b1
echo "b1" > b1file
svn add b1file
i=$(commit $i "b1 update 1")
cd ../..

say "Merge b1 to trunk"
svn update
cd trunk
svn merge ../branches/b1/ --accept postpone
i=$(commit $i "Merge b1 to trunk")
cd ..

say "Make a commit to trunk before merging trunk to b2"
svn update
cd trunk
echo "trunk" > trunkfile
svn add trunkfile
i=$(commit $i "trunk commit before merging trunk to b2")
cd ..

say "Merge trunk to b2"
svn update
cd branches/b2
svn merge ../../trunk/ --accept postpone
i=$(commit $i "Merge trunk to b2")
cd ../..

say "Merge b2 to trunk"
svn update
cd trunk
svn merge ../branches/b2/ --accept postpone
svn resolved b1file
svn resolved trunkfile
i=$(commit $i "Merge b2 to trunk")
cd ..

say "Creating f1 from trunk with a new file"
svn update
svn cp trunk branches/f1
cd branches/f1
echo "f1" > f1file
svn add f1file
cd ../..
i=$(commit $i "make f1 branch from trunk with a new file")

say "Creating f2 from trunk with a new file"
svn update
svn cp trunk branches/f2
cd branches/f2
echo "f2" > f2file
svn add f2file
cd ../..
i=$(commit $i "make f2 branch from trunk with a new file")

say "Merge f1 and f2 to trunk in one go"
svn update
cd trunk
svn merge ../branches/f1/ --accept postpone
svn merge ../branches/f2/ --accept postpone
i=$(commit $i "Merge f1 and f2 to trunk")
cd ..

say "Adding subdirectory to LEFT"
svn update
cd branches/left
mkdir subdir
echo "Yeehaw" > subdir/cowboy
svn add subdir
i=$(commit $i "add subdirectory to left branch")
cd ../../

say "Merging LEFT to TRUNK"
svn update
cd trunk
svn merge ../branches/left --accept postpone
i=$(commit $i "merge left to trunk")
cd ..

say "Make PARTIAL branch"
svn update
svn cp trunk/subdir branches/partial
i=$(commit $i "make partial branch")

say "Make a commit to PARTIAL"
svn update
cd branches/partial
echo "racecar" > palindromes
svn add palindromes
i=$(commit $i "partial update")
cd ../../

say "Merge PARTIAL to TRUNK"
svn update
cd trunk/subdir
svn merge ../../branches/partial --accept postpone
i=$(commit $i "merge partial to trunk")
cd ../../

say "Tagging trunk"
svn update
svn cp trunk tags/v1.0
i=$(commit $i "tagging v1.0")

say "Branching BUGFIX from v1.0"
svn update
svn cp tags/v1.0 branches/bugfix
i=$(commit $i "make bugfix branch from tag")

say "Make a commit to BUGFIX"
svn update
cd branches/bugfix/
echo "kayak" >> subdir/palindromes
i=$(commit $i "commit to bugfix")
cd ../../

say "Merge BUGFIX to TRUNK"
svn update
cd trunk
svn merge ../branches/bugfix/ --accept postpone
i=$(commit $i "Merge BUGFIX to TRUNK")
cd ..

cd ..
svnadmin dump foo.svn > svn-mergeinfo.dump

rm -rf foo foo.svn
