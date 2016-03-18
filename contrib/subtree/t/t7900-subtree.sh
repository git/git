#!/bin/sh
#
# Copyright (c) 2012 Avery Pennaraum
#
test_description='Basic porcelain support for subtrees

This test verifies the basic operation of the merge, pull, add
and split subcommands of git subtree.
'

TEST_DIRECTORY=$(pwd)/../../../t
export TEST_DIRECTORY

. ../../../t/test-lib.sh

create()
{
	echo "$1" >"$1"
	git add "$1"
}


check_equal()
{
	test_debug 'echo'
	test_debug "echo \"check a:\" \"{$1}\""
	test_debug "echo \"      b:\" \"{$2}\""
	if [ "$1" = "$2" ]; then
		return 0
	else
		return 1
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

test_expect_success 'init subproj' '
	test_create_repo subproj
'

# To the subproject!
cd subproj

test_expect_success 'add sub1' '
	create sub1 &&
	git commit -m "sub1" &&
	git branch sub1 &&
	git branch -m master subproj
'

# Save this hash for testing later.

subdir_hash=$(git rev-parse HEAD)

test_expect_success 'add sub2' '
	create sub2 &&
	git commit -m "sub2" &&
	git branch sub2
'

test_expect_success 'add sub3' '
	create sub3 &&
	git commit -m "sub3" &&
	git branch sub3
'

# Back to mainline
cd ..

test_expect_success 'enable log.date=relative to catch errors' '
	git config log.date relative
'

test_expect_success 'add main4' '
	create main4 &&
	git commit -m "main4" &&
	git branch -m master mainline &&
	git branch subdir
'

test_expect_success 'fetch subproj history' '
	git fetch ./subproj sub1 &&
	git branch sub1 FETCH_HEAD
'

test_expect_success 'no subtree exists in main tree' '
	test_must_fail git subtree merge --prefix=subdir sub1
'

test_expect_success 'no pull from non-existant subtree' '
	test_must_fail git subtree pull --prefix=subdir ./subproj sub1
'

test_expect_success 'check if --message works for add' '
	git subtree add --prefix=subdir --message="Added subproject" sub1 &&
	check_equal ''"$(last_commit_message)"'' "Added subproject" &&
	undo
'

test_expect_success 'check if --message works as -m and --prefix as -P' '
	git subtree add -P subdir -m "Added subproject using git subtree" sub1 &&
	check_equal ''"$(last_commit_message)"'' "Added subproject using git subtree" &&
	undo
'

test_expect_success 'check if --message works with squash too' '
	git subtree add -P subdir -m "Added subproject with squash" --squash sub1 &&
	check_equal ''"$(last_commit_message)"'' "Added subproject with squash" &&
	undo
'

test_expect_success 'add subproj to mainline' '
	git subtree add --prefix=subdir/ FETCH_HEAD &&
	check_equal ''"$(last_commit_message)"'' "Add '"'subdir/'"' from commit '"'"'''"$(git rev-parse sub1)"'''"'"'"
'

# this shouldn't actually do anything, since FETCH_HEAD is already a parent
test_expect_success 'merge fetched subproj' '
	git merge -m "merge -s -ours" -s ours FETCH_HEAD
'

test_expect_success 'add main-sub5' '
	create subdir/main-sub5 &&
	git commit -m "main-sub5"
'

test_expect_success 'add main6' '
	create main6 &&
	git commit -m "main6 boring"
'

test_expect_success 'add main-sub7' '
	create subdir/main-sub7 &&
	git commit -m "main-sub7"
'

test_expect_success 'fetch new subproj history' '
	git fetch ./subproj sub2 &&
	git branch sub2 FETCH_HEAD
'

test_expect_success 'check if --message works for merge' '
	git subtree merge --prefix=subdir -m "Merged changes from subproject" sub2 &&
	check_equal ''"$(last_commit_message)"'' "Merged changes from subproject" &&
	undo
'

test_expect_success 'check if --message for merge works with squash too' '
	git subtree merge --prefix subdir -m "Merged changes from subproject using squash" --squash sub2 &&
	check_equal ''"$(last_commit_message)"'' "Merged changes from subproject using squash" &&
	undo
'

test_expect_success 'merge new subproj history into subdir' '
	git subtree merge --prefix=subdir FETCH_HEAD &&
	git branch pre-split &&
	check_equal ''"$(last_commit_message)"'' "Merge commit '"'"'"$(git rev-parse sub2)"'"'"' into mainline" &&
	undo
'

test_expect_success 'Check that prefix argument is required for split' '
	echo "You must provide the --prefix option." > expected &&
	test_must_fail git subtree split > actual 2>&1 &&
	test_debug "printf '"'"'expected: '"'"'" &&
	test_debug "cat expected" &&
	test_debug "printf '"'"'actual: '"'"'" &&
	test_debug "cat actual" &&
	test_cmp expected actual &&
	rm -f expected actual
'

test_expect_success 'Check that the <prefix> exists for a split' '
	echo "'"'"'non-existent-directory'"'"'" does not exist\; use "'"'"'git subtree add'"'"'" > expected &&
	test_must_fail git subtree split --prefix=non-existent-directory > actual 2>&1 &&
	test_debug "printf '"'"'expected: '"'"'" &&
	test_debug "cat expected" &&
	test_debug "printf '"'"'actual: '"'"'" &&
	test_debug "cat actual" &&
	test_cmp expected actual
#	rm -f expected actual
'

test_expect_success 'check if --message works for split+rejoin' '
	spl1=''"$(git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --message "Split & rejoin" --rejoin)"'' &&
	git branch spl1 "$spl1" &&
	check_equal ''"$(last_commit_message)"'' "Split & rejoin" &&
	undo
'

test_expect_success 'check split with --branch' '
	spl1=$(git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --message "Split & rejoin" --rejoin) &&
	undo &&
	git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --branch splitbr1 &&
	check_equal ''"$(git rev-parse splitbr1)"'' "$spl1"
'

test_expect_success 'check hash of split' '
	spl1=$(git subtree split --prefix subdir) &&
	git subtree split --prefix subdir --branch splitbr1test &&
	check_equal ''"$(git rev-parse splitbr1test)"'' "$spl1" &&
	new_hash=$(git rev-parse splitbr1test~2) &&
	check_equal ''"$new_hash"'' "$subdir_hash"
'

test_expect_success 'check split with --branch for an existing branch' '
	spl1=''"$(git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --message "Split & rejoin" --rejoin)"'' &&
	undo &&
	git branch splitbr2 sub1 &&
	git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --branch splitbr2 &&
	check_equal ''"$(git rev-parse splitbr2)"'' "$spl1"
'

test_expect_success 'check split with --branch for an incompatible branch' '
	test_must_fail git subtree split --prefix subdir --onto FETCH_HEAD --branch subdir
'

test_expect_success 'check split+rejoin' '
	spl1=''"$(git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --message "Split & rejoin" --rejoin)"'' &&
	undo &&
	git subtree split --annotate='"'*'"' --prefix subdir --onto FETCH_HEAD --rejoin &&
	check_equal ''"$(last_commit_message)"'' "Split '"'"'subdir/'"'"' into commit '"'"'"$spl1"'"'"'"
'

test_expect_success 'add main-sub8' '
	create subdir/main-sub8 &&
	git commit -m "main-sub8"
'

# To the subproject!
cd ./subproj

test_expect_success 'merge split into subproj' '
	git fetch .. spl1 &&
	git branch spl1 FETCH_HEAD &&
	git merge FETCH_HEAD
'

test_expect_success 'add sub9' '
	create sub9 &&
	git commit -m "sub9"
'

# Back to mainline
cd ..

test_expect_success 'split for sub8' '
	split2=''"$(git subtree split --annotate='"'*'"' --prefix subdir/ --rejoin)"'' &&
	git branch split2 "$split2"
'

test_expect_success 'add main-sub10' '
	create subdir/main-sub10 &&
	git commit -m "main-sub10"
'

test_expect_success 'split for sub10' '
	spl3=''"$(git subtree split --annotate='"'*'"' --prefix subdir --rejoin)"'' &&
	git branch spl3 "$spl3"
'

# To the subproject!
cd ./subproj

test_expect_success 'merge split into subproj' '
	git fetch .. spl3 &&
	git branch spl3 FETCH_HEAD &&
	git merge FETCH_HEAD &&
	git branch subproj-merge-spl3
'

chkm="main4 main6"
chkms="main-sub10 main-sub5 main-sub7 main-sub8"
chkms_sub=$(echo $chkms | multiline | sed 's,^,subdir/,' | fixnl)
chks="sub1 sub2 sub3 sub9"
chks_sub=$(echo $chks | multiline | sed 's,^,subdir/,' | fixnl)

test_expect_success 'make sure exactly the right set of files ends up in the subproj' '
	subfiles=''"$(git ls-files | fixnl)"'' &&
	check_equal "$subfiles" "$chkms $chks"
'

test_expect_success 'make sure the subproj history *only* contains commits that affect the subdir' '
	allchanges=''"$(git log --name-only --pretty=format:'"''"' | sort | fixnl)"'' &&
	check_equal "$allchanges" "$chkms $chks"
'

# Back to mainline
cd ..

test_expect_success 'pull from subproj' '
	git fetch ./subproj subproj-merge-spl3 &&
	git branch subproj-merge-spl3 FETCH_HEAD &&
	git subtree pull --prefix=subdir ./subproj subproj-merge-spl3
'

test_expect_success 'make sure exactly the right set of files ends up in the mainline' '
	mainfiles=''"$(git ls-files | fixnl)"'' &&
	check_equal "$mainfiles" "$chkm $chkms_sub $chks_sub"
'

test_expect_success 'make sure each filename changed exactly once in the entire history' '
	# main-sub?? and /subdir/main-sub?? both change, because those are the
	# changes that were split into their own history.  And subdir/sub?? never
	# change, since they were *only* changed in the subtree branch.
	allchanges=''"$(git log --name-only --pretty=format:'"''"' | sort | fixnl)"'' &&
	check_equal "$allchanges" ''"$(echo $chkms $chkm $chks $chkms_sub | multiline | sort | fixnl)"''
'

test_expect_success 'make sure the --rejoin commits never make it into subproj' '
	check_equal ''"$(git log --pretty=format:'"'%s'"' HEAD^2 | grep -i split)"'' ""
'

test_expect_success 'make sure no "git subtree" tagged commits make it into subproj' '
	# They are meaningless to subproj since one side of the merge refers to the mainline
	check_equal ''"$(git log --pretty=format:'"'%s%n%b'"' HEAD^2 | grep "git-subtree.*:")"'' ""
'

# prepare second pair of repositories
mkdir test2
cd test2

test_expect_success 'init main' '
	test_create_repo main
'

cd main

test_expect_success 'add main1' '
	create main1 &&
	git commit -m "main1"
'

cd ..

test_expect_success 'init sub' '
	test_create_repo sub
'

cd sub

test_expect_success 'add sub2' '
	create sub2 &&
	git commit -m "sub2"
'

cd ../main

# check if split can find proper base without --onto

test_expect_success 'add sub as subdir in main' '
	git fetch ../sub master &&
	git branch sub2 FETCH_HEAD &&
	git subtree add --prefix subdir sub2
'

cd ../sub

test_expect_success 'add sub3' '
	create sub3 &&
	git commit -m "sub3"
'

cd ../main

test_expect_success 'merge from sub' '
	git fetch ../sub master &&
	git branch sub3 FETCH_HEAD &&
	git subtree merge --prefix subdir sub3
'

test_expect_success 'add main-sub4' '
	create subdir/main-sub4 &&
	git commit -m "main-sub4"
'

test_expect_success 'split for main-sub4 without --onto' '
	git subtree split --prefix subdir --branch mainsub4
'

# at this point, the new commit parent should be sub3 if it is not,
# something went wrong (the "newparent" of "master~" commit should
# have been sub3, but it was not, because its cache was not set to
# itself)

test_expect_success 'check that the commit parent is sub3' '
	check_equal ''"$(git log --pretty=format:%P -1 mainsub4)"'' ''"$(git rev-parse sub3)"''
'

test_expect_success 'add main-sub5' '
	mkdir subdir2 &&
	create subdir2/main-sub5 &&
	git commit -m "main-sub5"
'

test_expect_success 'split for main-sub5 without --onto' '
	# also test that we still can split out an entirely new subtree
	# if the parent of the first commit in the tree is not empty,
	# then the new subtree has accidentally been attached to something
	git subtree split --prefix subdir2 --branch mainsub5 &&
	check_equal ''"$(git log --pretty=format:%P -1 mainsub5)"'' ""
'

# make sure no patch changes more than one file.  The original set of commits
# changed only one file each.  A multi-file change would imply that we pruned
# commits too aggressively.
joincommits()
{
	commit=
	all=
	while read x y; do
		#echo "{$x}" >&2
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

test_expect_success 'verify one file change per commit' '
	x= &&
	list=''"$(git log --pretty=format:'"'commit: %H'"' | joincommits)"'' &&
#	test_debug "echo HERE" &&
#	test_debug "echo ''"$list"''" &&
	(git log --pretty=format:'"'commit: %H'"' | joincommits |
	(	while read commit a b; do
			test_debug "echo Verifying commit "''"$commit"''
			test_debug "echo a: "''"$a"''
			test_debug "echo b: "''"$b"''
			check_equal "$b" ""
			x=1
		done
		check_equal "$x" 1
	))
'

test_done
