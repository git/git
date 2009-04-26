#!/bin/bash
. shellopts.sh
set -e

create()
{
	echo "$1" >"$1"
	git add "$1"
}

check()
{
	echo
	echo "check:" "$@"
	if "$@"; then
		echo ok
		return 0
	else
		echo FAILED
		exit 1
	fi
}

check_equal()
{
	echo
	echo "check a:" "{$1}"
	echo "      b:" "{$2}"
	if [ "$1" = "$2" ]; then
		return 0
	else
		echo FAILED
		exit 1
	fi
}

fixnl()
{	
	t=""
	while read x; do
		t="$t$x "
	done
	echo $t
}

multiline()
{
	while read x; do
		set -- $x
		for d in "$@"; do
			echo "$d"
		done
	done
}

rm -rf mainline subproj
mkdir mainline subproj

cd subproj
git init

create sub1
git commit -m 'sub1'
git branch sub1
git branch -m master subproj
check true

create sub2
git commit -m 'sub2'
git branch sub2

create sub3
git commit -m 'sub3'
git branch sub3

cd ../mainline
git init
create main4
git commit -m 'main4'
git branch -m master mainline

git fetch ../subproj sub1
git branch sub1 FETCH_HEAD
git subtree add --prefix=subdir FETCH_HEAD

# this shouldn't actually do anything, since FETCH_HEAD is already a parent
git merge -m 'merge -s -ours' -s ours FETCH_HEAD

create subdir/main-sub5
git commit -m 'main-sub5'

create main6
git commit -m 'main6 boring'

create subdir/main-sub7
git commit -m 'main-sub7'

git fetch ../subproj sub2
git branch sub2 FETCH_HEAD
git subtree merge --prefix=subdir FETCH_HEAD
git branch pre-split

spl1=$(git subtree split --annotate='*' \
		--prefix subdir --onto FETCH_HEAD --rejoin)
echo "spl1={$spl1}"
git branch spl1 "$spl1"

create subdir/main-sub8
git commit -m 'main-sub8'

cd ../subproj
git fetch ../mainline spl1
git branch spl1 FETCH_HEAD
git merge FETCH_HEAD

create sub9
git commit -m 'sub9'

cd ../mainline
split2=$(git subtree split --annotate='*' --prefix subdir --rejoin)
git branch split2 "$split2"

create subdir/main-sub10
git commit -m 'main-sub10'

spl3=$(git subtree split --annotate='*' --prefix subdir --rejoin)
git branch spl3 "$spl3"

cd ../subproj
git fetch ../mainline spl3
git branch spl3 FETCH_HEAD
git merge FETCH_HEAD
git branch subproj-merge-spl3

chkm="main4 main6"
chkms="main-sub10 main-sub5 main-sub7 main-sub8"
chkms_sub=$(echo $chkms | multiline | sed 's,^,subdir/,' | fixnl)
chks="sub1 sub2 sub3 sub9"
chks_sub=$(echo $chks | multiline | sed 's,^,subdir/,' | fixnl)

# make sure exactly the right set of files ends up in the subproj
subfiles=$(git ls-files | fixnl)
check_equal "$subfiles" "$chkms $chks"

# make sure the subproj history *only* contains commits that affect the subdir.
allchanges=$(git log --name-only --pretty=format:'' | sort | fixnl)
check_equal "$allchanges" "$chkms $chks"

cd ../mainline
git fetch ../subproj subproj-merge-spl3
git branch subproj-merge-spl3 FETCH_HEAD
git subtree pull --prefix=subdir ../subproj subproj-merge-spl3

# make sure exactly the right set of files ends up in the mainline
mainfiles=$(git ls-files | fixnl)
check_equal "$mainfiles" "$chkm $chkms_sub $chks_sub"

# make sure each filename changed exactly once in the entire history.
# 'main-sub??' and '/subdir/main-sub??' both change, because those are the
# changes that were split into their own history.  And 'subdir/sub??' never
# change, since they were *only* changed in the subtree branch.
allchanges=$(git log --name-only --pretty=format:'' | sort | fixnl)
check_equal "$allchanges" "$chkm $chkms $chks $chkms_sub"

# make sure the --rejoin commits never make it into subproj
check_equal "$(git log --pretty=format:'%s' HEAD^2 | grep -i split)" ""

# make sure no 'git subtree' tagged commits make it into subproj. (They're
# meaningless to subproj since one side of the merge refers to the mainline)
check_equal "$(git log --pretty=format:'%s%n%b' HEAD^2 | grep 'git-subtree.*:')" ""

echo
echo 'ok'
