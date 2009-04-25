#!/bin/bash -x
. shellopts.sh
set -e

rm -rf mainline subproj
mkdir mainline subproj

cd subproj
git init

touch sub1
git add sub1
git commit -m 'sub-1'
git branch sub1
git branch -m master subproj

touch sub2
git add sub2
git commit -m 'sub-2'
git branch sub2

touch sub3
git add sub3
git commit -m 'sub-3'
git branch sub3

cd ../mainline
git init
touch main1
git add main1
git commit -m 'main-1'
git branch -m master mainline

git fetch ../subproj sub1
git branch sub1 FETCH_HEAD
git read-tree --prefix=subdir FETCH_HEAD
git checkout subdir
git commit -m 'initial-subdir-merge'

git merge -m 'merge -s -ours' -s ours FETCH_HEAD

touch subdir/main-sub3
git add subdir/main-sub3
git commit -m 'main-sub3'

touch main-2
git add main-2
git commit -m 'main-2 boring'

touch subdir/main-sub4
git add subdir/main-sub4
git commit -m 'main-sub4'

git fetch ../subproj sub2
git branch sub2 FETCH_HEAD
git merge -s subtree FETCH_HEAD
git branch pre-split

split1=$(git subtree split --onto FETCH_HEAD subdir --rejoin)
echo "split1={$split1}"
git branch split1 "$split1"

touch subdir/main-sub5
git add subdir/main-sub5
git commit -m 'main-sub5'

cd ../subproj
git fetch ../mainline split1
git branch split1 FETCH_HEAD
git merge FETCH_HEAD

touch sub6
git add sub6
git commit -m 'sub6'

cd ../mainline
split2=$(git subtree split subdir --rejoin)
git branch split2 "$split2"

touch subdir/main-sub7
git add subdir/main-sub7
git commit -m 'main-sub7'

split3=$(git subtree split subdir --rejoin)
git branch split3 "$split3"

cd ../subproj
git fetch ../mainline split3
git branch split3 FETCH_HEAD
git merge FETCH_HEAD
git branch subproj-merge-split3

cd ../mainline
git fetch ../subproj subproj-merge-split3
git branch subproj-merge-split3 FETCH_HEAD
git merge subproj-merge-split3
