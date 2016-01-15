#!/bin/sh
#
# Copyright (c) 2012 Avery Pennaraum
# Copyright (c) 2015 Alexey Shumkin
#
test_description='Basic porcelain support for subtrees

This test verifies the basic operation of the add, pull, merge
and split subcommands of git subtree.
'

TEST_DIRECTORY=$(pwd)/../../../t
export TEST_DIRECTORY

. ../../../t/test-lib.sh

subtree_test_create_repo()
{
	test_create_repo "$1"
	(
		cd $1
		git config log.date relative
	)
}

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

undo()
{
	git reset --hard HEAD~
}

# Make sure no patch changes more than one file.
# The original set of commits changed only one file each.
# A multi-file change would imply that we pruned commits
# too aggressively.
join_commits()
{
	commit=
	all=
	while read x y; do
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

test_create_commit() (
	repo=$1
	commit=$2
	cd "$repo"
	mkdir -p $(dirname "$commit") \
	|| error "Could not create directory for commit"
	echo "$commit" >"$commit"
	git add "$commit" || error "Could not add commit"
	git commit -m "$commit" || error "Could not commit"
)

last_commit_message()
{
	git log --pretty=format:%s -1
}

subtree_test_count=0
next_test() {
	subtree_test_count=$(($subtree_test_count+1))
}

#
# Tests for 'git subtree add'
#

next_test
test_expect_success 'no merge from non-existent subtree' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		test_must_fail git subtree merge --prefix="sub dir" FETCH_HEAD
	)
'

next_test
test_expect_success 'no pull from non-existent subtree' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		test_must_fail git subtree pull --prefix="sub dir" ./"sub proj" master
	)'

next_test
test_expect_success 'add subproj as subtree into sub dir/ with --prefix' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Add '\''sub dir/'\'' from commit '\''$(git rev-parse FETCH_HEAD)'\''"
	)
'

next_test
test_expect_success 'add subproj as subtree into sub dir/ with --prefix and --message' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" --message="Added subproject" FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Added subproject"
	)
'

next_test
test_expect_success 'add subproj as subtree into sub dir/ with --prefix as -P and --message as -m' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add -P "sub dir" -m "Added subproject" FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Added subproject"
	)
'

next_test
test_expect_success 'add subproj as subtree into sub dir/ with --squash and --prefix and --message' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" --message="Added subproject with squash" --squash FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Added subproject with squash"
	)
'

#
# Tests for 'git subtree merge'
#

next_test
test_expect_success 'merge new subproj history into sub dir/ with --prefix' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Merge commit '\''$(git rev-parse FETCH_HEAD)'\''"
	)
'

next_test
test_expect_success 'merge new subproj history into sub dir/ with --prefix and --message' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" --message="Merged changes from subproject" FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Merged changes from subproject"
	)
'

next_test
test_expect_success 'merge new subproj history into sub dir/ with --squash and --prefix and --message' '
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	subtree_test_create_repo "$subtree_test_count" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" --message="Merged changes from subproject using squash" --squash FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Merged changes from subproject using squash"
	)
'

next_test
test_expect_success 'merge the added subproj again, should do nothing' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		# this shouldn not actually do anything, since FETCH_HEAD
		# is already a parent
		result=$(git merge -s ours -m "merge -s -ours" FETCH_HEAD) &&
		check_equal "${result}" "Already up-to-date."
	)
'

next_test
test_expect_success 'merge new subproj history into subdir/ with a slash appended to the argument of --prefix' '
	test_create_repo "$test_count" &&
	test_create_repo "$test_count/subproj" &&
	test_create_commit "$test_count" main1 &&
	test_create_commit "$test_count/subproj" sub1 &&
	(
		cd "$test_count" &&
		git fetch ./subproj master &&
		git subtree add --prefix=subdir/ FETCH_HEAD
	) &&
	test_create_commit "$test_count/subproj" sub2 &&
	(
		cd "$test_count" &&
		git fetch ./subproj master &&
		git subtree merge --prefix=subdir/ FETCH_HEAD &&
		check_equal "$(last_commit_message)" "Merge commit '\''$(git rev-parse FETCH_HEAD)'\''"
	)
'

#
# Tests for 'git subtree split'
#

next_test
test_expect_success 'split requires option --prefix' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "You must provide the --prefix option." > expected &&
		test_must_fail git subtree split > actual 2>&1 &&
		test_debug "printf '"expected: "'" &&
		test_debug "cat expected" &&
		test_debug "printf '"actual: "'" &&
		test_debug "cat actual" &&
		test_cmp expected actual
	)
'

next_test
test_expect_success 'split requires path given by option --prefix must exist' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD &&
		echo "'\''non-existent-directory'\'' does not exist; use '\''git subtree add'\''" > expected &&
		test_must_fail git subtree split --prefix=non-existent-directory > actual 2>&1 &&
		test_debug "printf '"expected: "'" &&
		test_debug "cat expected" &&
		test_debug "printf '"actual: "'" &&
		test_debug "cat actual" &&
		test_cmp expected actual
	)
'

next_test
test_expect_success 'split sub dir/ with --rejoin' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --rejoin &&
		check_equal "$(last_commit_message)" "Split '\''sub dir/'\'' into commit '\''$split_hash'\''"
	)
 '

next_test
test_expect_success 'split sub dir/ with --rejoin and --message' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --message="Split & rejoin" --annotate="*" --rejoin &&
		check_equal "$(last_commit_message)" "Split & rejoin"
	)
'

next_test
test_expect_success 'split "sub dir"/ with --branch' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		check_equal "$(git rev-parse subproj-br)" "$split_hash"
	)
'

next_test
test_expect_success 'check hash of split' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		check_equal "$(git rev-parse subproj-br)" "$split_hash" &&
		# Check hash of split
		new_hash=$(git rev-parse subproj-br^2) &&
		(
			cd ./"sub proj" &&
			subdir_hash=$(git rev-parse HEAD) &&
			check_equal ''"$new_hash"'' "$subdir_hash"
		)
	)
'

next_test
test_expect_success 'split "sub dir"/ with --branch for an existing branch' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git branch subproj-br FETCH_HEAD &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		split_hash=$(git subtree split --prefix="sub dir" --annotate="*") &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br &&
		check_equal "$(git rev-parse subproj-br)" "$split_hash"
	)
'

next_test
test_expect_success 'split "sub dir"/ with --branch for an incompatible branch' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git branch init HEAD &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		test_must_fail git subtree split --prefix="sub dir" --branch init
	)
'

#
# Validity checking
#

next_test
test_expect_success 'make sure exactly the right set of files ends up in the subproj' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub3 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD &&

		chks="sub1
sub2
sub3
sub4" &&
		chks_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chks
TXT
) &&
		chkms="main-sub1
main-sub2
main-sub3
main-sub4" &&
		chkms_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chkms
TXT
) &&

		subfiles=$(git ls-files) &&
		check_equal "$subfiles" "$chkms
$chks"
	)
'

next_test
test_expect_success 'make sure the subproj *only* contains commits that affect the "sub dir"' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub3 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD &&

		chks="sub1
sub2
sub3
sub4" &&
		chks_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chks
TXT
) &&
		chkms="main-sub1
main-sub2
main-sub3
main-sub4" &&
		chkms_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chkms
TXT
) &&
		allchanges=$(git log --name-only --pretty=format:"" | sort | sed "/^$/d") &&
		check_equal "$allchanges" "$chkms
$chks"
	)
'

next_test
test_expect_success 'make sure exactly the right set of files ends up in the mainline' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub3 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$subtree_test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" master &&

		chkm="main1
main2" &&
		chks="sub1
sub2
sub3
sub4" &&
		chks_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chks
TXT
) &&
		chkms="main-sub1
main-sub2
main-sub3
main-sub4" &&
		chkms_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chkms
TXT
) &&
		mainfiles=$(git ls-files) &&
		check_equal "$mainfiles" "$chkm
$chkms_sub
$chks_sub"
)
'

next_test
test_expect_success 'make sure each filename changed exactly once in the entire history' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git config log.date relative
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub3 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$subtree_test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" master &&

		chkm="main1
main2" &&
		chks="sub1
sub2
sub3
sub4" &&
		chks_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chks
TXT
) &&
		chkms="main-sub1
main-sub2
main-sub3
main-sub4" &&
		chkms_sub=$(cat <<TXT | sed '\''s,^,sub dir/,'\''
$chkms
TXT
) &&

		# main-sub?? and /"sub dir"/main-sub?? both change, because those are the
		# changes that were split into their own history.  And "sub dir"/sub?? never
		# change, since they were *only* changed in the subtree branch.
		allchanges=$(git log --name-only --pretty=format:"" | sort | sed "/^$/d") &&
		expected=''"$(cat <<TXT | sort
$chkms
$chkm
$chks
$chkms_sub
TXT
)"'' &&
		check_equal "$allchanges" "$expected"
	)
'

next_test
test_expect_success 'make sure the --rejoin commits never make it into subproj' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub3 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$subtree_test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" master &&
		check_equal "$(git log --pretty=format:"%s" HEAD^2 | grep -i split)" ""
	)
'

next_test
test_expect_success 'make sure no "git subtree" tagged commits make it into subproj' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub3 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		 git merge FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub4 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --annotate="*" --branch subproj-br --rejoin
	) &&
	(
		cd "$subtree_test_count/sub proj" &&
		git fetch .. subproj-br &&
		git merge FETCH_HEAD
	) &&
	(
		cd "$subtree_test_count" &&
		git subtree pull --prefix="sub dir" ./"sub proj" master &&

		# They are meaningless to subproj since one side of the merge refers to the mainline
		check_equal "$(git log --pretty=format:"%s%n%b" HEAD^2 | grep "git-subtree.*:")" ""
	)
'

#
# A new set of tests
#

next_test
test_expect_success 'make sure "git subtree split" find the correct parent' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git branch subproj-ref FETCH_HEAD &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --branch subproj-br &&

		# at this point, the new commit parent should be subproj-ref, if it is
		# not, something went wrong (the "newparent" of "master~" commit should
		# have been sub2, but it was not, because its cache was not set to
		# itself)
		check_equal "$(git log --pretty=format:%P -1 subproj-br)" "$(git rev-parse subproj-ref)"
	)
'

next_test
test_expect_success 'split a new subtree without --onto option' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --branch subproj-br
	) &&
	mkdir "$subtree_test_count"/"sub dir2" &&
	test_create_commit "$subtree_test_count" "sub dir2"/main-sub2 &&
	(
		cd "$subtree_test_count" &&

		# also test that we still can split out an entirely new subtree
		# if the parent of the first commit in the tree is not empty,
		# then the new subtree has accidently been attached to something
		git subtree split --prefix="sub dir2" --branch subproj2-br &&
		check_equal "$(git log --pretty=format:%P -1 subproj2-br)" ""
	)
'

next_test
test_expect_success 'verify one file change per commit' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git branch sub1 FETCH_HEAD &&
		git subtree add --prefix="sub dir" sub1
	) &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir" --branch subproj-br
	) &&
	mkdir "$subtree_test_count"/"sub dir2" &&
	test_create_commit "$subtree_test_count" "sub dir2"/main-sub2 &&
	(
		cd "$subtree_test_count" &&
		git subtree split --prefix="sub dir2" --branch subproj2-br &&

		x= &&
		git log --pretty=format:"commit: %H" | join_commits |
		(
			while read commit a b; do
				test_debug "echo Verifying commit $commit"
				test_debug "echo a: $a"
				test_debug "echo b: $b"
				check_equal "$b" ""
				x=1
			done
			check_equal "$x" 1
		)
	)
'

next_test
test_expect_success 'push split to subproj' '
	subtree_test_create_repo "$subtree_test_count" &&
	subtree_test_create_repo "$subtree_test_count/sub proj" &&
	test_create_commit "$subtree_test_count" main1 &&
	test_create_commit "$subtree_test_count/sub proj" sub1 &&
	(
		cd "$subtree_test_count" &&
		git fetch ./"sub proj" master &&
		git subtree add --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub1 &&
	test_create_commit "$subtree_test_count" main2 &&
	test_create_commit "$subtree_test_count/sub proj" sub2 &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub2 &&
	(
		cd $subtree_test_count/"sub proj" &&
                git branch sub-branch-1 &&
                cd .. &&
		git fetch ./"sub proj" master &&
		git subtree merge --prefix="sub dir" FETCH_HEAD
	) &&
	test_create_commit "$subtree_test_count" "sub dir"/main-sub3 &&
        (
		cd "$subtree_test_count" &&
	        git subtree push ./"sub proj" --prefix "sub dir" sub-branch-1 &&
                cd ./"sub proj" &&
                git checkout sub-branch-1 &&
         	check_equal "$(last_commit_message)" "sub dir/main-sub3"
	)
'

#
# This test covers 2 cases in subtree split copy_or_skip code
# 1) Merges where one parent is a superset of the changes of the other
#    parent regarding changes to the subtree, in this case the merge
#    commit should be copied
# 2) Merges where only one parent operate on the subtree, and the merge
#    commit should be skipped
#
# (1) is checked by ensuring subtree_tip is a descendent of subtree_branch
# (2) should have a check added (not_a_subtree_change shouldn't be present
#     on the produced subtree)
#
# Other related cases which are not tested (or currently handled correctly)
# - Case (1) where there are more than 2 parents, it will sometimes correctly copy
#   the merge, and sometimes not
# - Merge commit where both parents have same tree as the merge, currently
#   will always be skipped, even if they reached that state via different
#   set of commits.
#

next_test
test_expect_success 'subtree descendant check' '
	subtree_test_create_repo "$subtree_test_count" &&
	test_create_commit "$subtree_test_count" folder_subtree/a &&
	(
		cd "$subtree_test_count" &&
		git branch branch
	) &&
	test_create_commit "$subtree_test_count" folder_subtree/0 &&
	test_create_commit "$subtree_test_count" folder_subtree/b &&
	cherry=$(cd "$subtree_test_count"; git rev-parse HEAD) &&
	(
		cd "$subtree_test_count" &&
		git checkout branch
	) &&
	test_create_commit "$subtree_test_count" commit_on_branch &&
	(
		cd "$subtree_test_count" &&
		git cherry-pick $cherry &&
		git checkout master &&
		git merge -m "merge should be kept on subtree" branch &&
		git branch no_subtree_work_branch
	) &&
	test_create_commit "$subtree_test_count" folder_subtree/d &&
	(
		cd "$subtree_test_count" &&
		git checkout no_subtree_work_branch
	) &&
	test_create_commit "$subtree_test_count" not_a_subtree_change &&
	(
		cd "$subtree_test_count" &&
		git checkout master &&
		git merge -m "merge should be skipped on subtree" no_subtree_work_branch &&

		git subtree split --prefix folder_subtree/ --branch subtree_tip master &&
		git subtree split --prefix folder_subtree/ --branch subtree_branch branch &&
		check_equal $(git rev-list --count subtree_tip..subtree_branch) 0
	)
'

test_done
