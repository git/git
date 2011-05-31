#!/bin/sh
#
# this script sets up a Subversion repository for Makefile in the
# first ever git merge, as if it were done with svk.
#

set -e

svk depotmap foo ~/.svk/foo
svk co /foo/ foo
cd foo
mkdir trunk
mkdir branches
svk add trunk branches
svk commit -m "Setup trunk and branches"
cd trunk

git cat-file blob 6683463e:Makefile > Makefile
svk add Makefile 

svk commit -m "ancestor"
cd ..
svk cp trunk branches/left

svk commit -m "make left branch"
cd branches/left/

git cat-file blob 5873b67e:Makefile > Makefile
svk commit -m "left update 1"

cd ../../trunk
git cat-file blob 75118b13:Makefile > Makefile
svk commit -m "trunk update"

cd ../branches/left
git cat-file blob b5039db6:Makefile > Makefile
svk commit -m "left update 2"

cd ../../trunk
svk sm /foo/branches/left
# in theory we could delete the "left" branch here, but it's not
# required so don't do it, in case people start getting ideas ;)
svk commit -m "merge branch 'left' into 'trunk'"

git cat-file blob b51ad431:Makefile > Makefile

svk diff Makefile && echo "Hey!  No differences, magic"

cd ../..

svnadmin dump ~/.svk/foo > svk-merge.dump

svk co -d foo
rm -rf foo
svk depotmap -d /foo/
rm -rf ~/.svk/foo

