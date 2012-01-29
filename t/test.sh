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

check_not()
{
	echo
	echo "check: NOT " "$@"
	if "$@"; then
		echo FAILED
		exit 1
	else
		echo ok
		return 0
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

undo()
{
	git reset --hard HEAD~
}

last_commit_message()
{
	git log --pretty=format:%s -1
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
git branch subdir

git fetch ../subproj sub1
git branch sub1 FETCH_HEAD

# check if --message works for add
check_not git subtree merge --prefix=subdir sub1
check_not git subtree pull --prefix=subdir ../subproj sub1
git subtree add --prefix=subdir --message="Added subproject" sub1
check_equal "$(last_commit_message)" "Added subproject"
undo

# check if --message works as -m and --prefix as -P
git subtree add -P subdir -m "Added subproject using git subtree" sub1
check_equal "$(last_commit_message)" "Added subproject using git subtree"
undo

# check if --message works with squash too
git subtree add -P subdir -m "Added subproject with squash" --squash sub1
check_equal "$(last_commit_message)" "Added subproject with squash"
undo

git subtree add --prefix=subdir/ FETCH_HEAD
check_equal "$(last_commit_message)" "Add 'subdir/' from commit '$(git rev-parse sub1)'"

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

# check if --message works for merge
git subtree merge --prefix=subdir -m "Merged changes from subproject" sub2
check_equal "$(last_commit_message)" "Merged changes from subproject"
undo

# check if --message for merge works with squash too
git subtree merge --prefix subdir -m "Merged changes from subproject using squash" --squash sub2
check_equal "$(last_commit_message)" "Merged changes from subproject using squash"
undo

git subtree merge --prefix=subdir FETCH_HEAD
git branch pre-split
check_equal "$(last_commit_message)" "Merge commit '$(git rev-parse sub2)' into mainline"

# Check that prefix argument is required for split (exits with warning and exit status = 1)
! result=$(git subtree split 2>&1)
check_equal "You must provide the --prefix option." "$result"

# Check that the <prefix> exists for a split.
! result=$(git subtree split --prefix=non-existent-directory 2>&1)
check_equal "'non-existent-directory' does not exist; use 'git subtree add'" \
  "$result"

# check if --message works for split+rejoin
spl1=$(git subtree split --annotate='*' --prefix subdir --onto FETCH_HEAD --message "Split & rejoin" --rejoin)
echo "spl1={$spl1}"
git branch spl1 "$spl1"
check_equal "$(last_commit_message)" "Split & rejoin"
undo

# check split with --branch
git subtree split --annotate='*' --prefix subdir --onto FETCH_HEAD --branch splitbr1
check_equal "$(git rev-parse splitbr1)" "$spl1"

# check split with --branch for an existing branch
git branch splitbr2 sub1
git subtree split --annotate='*' --prefix subdir --onto FETCH_HEAD --branch splitbr2
check_equal "$(git rev-parse splitbr2)" "$spl1"

# check split with --branch for an incompatible branch
result=$(git subtree split --prefix subdir --onto FETCH_HEAD --branch subdir || echo "caught error")
check_equal "$result" "caught error"


git subtree split --annotate='*' --prefix subdir --onto FETCH_HEAD --rejoin
check_equal "$(last_commit_message)" "Split 'subdir/' into commit '$spl1'"

create subdir/main-sub8
git commit -m 'main-sub8'

cd ../subproj
git fetch ../mainline spl1
git branch spl1 FETCH_HEAD
git merge FETCH_HEAD

create sub9
git commit -m 'sub9'

cd ../mainline
split2=$(git subtree split --annotate='*' --prefix subdir/ --rejoin)
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
check_equal "$allchanges" "$(echo $chkms $chkm $chks $chkms_sub | multiline | sort | fixnl)"

# make sure the --rejoin commits never make it into subproj
check_equal "$(git log --pretty=format:'%s' HEAD^2 | grep -i split)" ""

# make sure no 'git subtree' tagged commits make it into subproj. (They're
# meaningless to subproj since one side of the merge refers to the mainline)
check_equal "$(git log --pretty=format:'%s%n%b' HEAD^2 | grep 'git-subtree.*:')" ""


# check if split can find proper base without --onto
# prepare second pair of repositories
mkdir test2
cd test2

mkdir main
cd main
git init
create main1
git commit -m "main1"

cd ..
mkdir sub
cd sub
git init
create sub2
git commit -m "sub2"

cd ../main
git fetch ../sub master
git branch sub2 FETCH_HEAD
git subtree add --prefix subdir sub2

cd ../sub
create sub3
git commit -m "sub3"

cd ../main
git fetch ../sub master
git branch sub3 FETCH_HEAD
git subtree merge --prefix subdir sub3

create subdir/main-sub4
git commit -m "main-sub4"
git subtree split --prefix subdir --branch mainsub4

# at this point, the new commit's parent should be sub3
# if it's not, something went wrong (the "newparent" of "master~" commit should have been sub3,
# but it wasn't, because it's cache was not set to itself)
check_equal "$(git log --pretty=format:%P -1 mainsub4)" "$(git rev-parse sub3)"

mkdir subdir2
create subdir2/main-sub5
git commit -m "main-sub5"
git subtree split --prefix subdir2 --branch mainsub5

# also test that we still can split out an entirely new subtree
# if the parent of the first commit in the tree isn't empty,
# then the new subtree has accidently been attached to something
check_equal "$(git log --pretty=format:%P -1 mainsub5)" ""


# make sure no patch changes more than one file.  The original set of commits
# changed only one file each.  A multi-file change would imply that we pruned
# commits too aggressively.
joincommits()
{
	commit=
	all=
	while read x y; do
		echo "{$x}" >&2
		if [ -z "$x" ]; then
			continue
		elif [ "$x" = "commit:" ]; then
			if [ -n "$commit" ]; then
				echo "$commit $all"
				all=
			fi
			commit="$y"
		else
			all="$all $y"
		fi
	done
	echo "$commit $all"
}
x=
git log --pretty=format:'commit: %H' | joincommits |
(	while read commit a b; do
		echo "Verifying commit $commit"
		check_equal "$b" ""
		x=1
	done
	check_equal "$x" 1
) || exit 1

echo
echo 'ok'
