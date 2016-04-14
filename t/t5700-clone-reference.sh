#!/bin/sh
#
# Copyright (C) 2006 Martin Waitz <tali@admingilde.org>
#

test_description='test clone --reference'
. ./test-lib.sh

base_dir=$(pwd)

U=$base_dir/UPLOAD_LOG

# create a commit in repo $1 with name $2
commit_in () {
	(
		cd "$1" &&
		echo "$2" >"$2" &&
		git add "$2" &&
		git commit -m "$2"
	)
}

# check that there are $2 loose objects in repo $1
test_objcount () {
	echo "$2" >expect &&
	git -C "$1" count-objects >actual.raw &&
	cut -d' ' -f1 <actual.raw >actual &&
	test_cmp expect actual
}

test_expect_success 'preparing first repository' '
	test_create_repo A &&
	commit_in A file1
'

test_expect_success 'preparing second repository' '
	git clone A B &&
	commit_in B file2 &&
	git -C B repack -ad &&
	git -C B prune
'

test_expect_success 'cloning with reference (-l -s)' '
	git clone -l -s --reference B A C
'

test_expect_success 'existence of info/alternates' '
	test_line_count = 2 C/.git/objects/info/alternates
'

test_expect_success 'pulling from reference' '
	git -C C pull ../B master
'

test_expect_success 'that reference gets used' '
	test_objcount C 0
'

test_expect_success 'cloning with reference (no -l -s)' '
	GIT_TRACE_PACKET=$U.D git clone --reference B "file://$(pwd)/A" D
'

test_expect_success 'fetched no objects' '
	test -s "$U.D" &&
	! grep " want" "$U.D"
'

test_expect_success 'existence of info/alternates' '
	test_line_count = 1 D/.git/objects/info/alternates
'

test_expect_success 'pulling from reference' '
	git -C D pull ../B master
'

test_expect_success 'that reference gets used' '
	test_objcount D 0
'

test_expect_success 'updating origin' '
	commit_in A file3 &&
	git -C A repack -ad &&
	git -C A prune
'

test_expect_success 'pulling changes from origin' '
	git -C C pull origin
'

# the 2 local objects are commit and tree from the merge
test_expect_success 'that alternate to origin gets used' '
	test_objcount C 2
'

test_expect_success 'pulling changes from origin' '
	git -C D pull origin
'

# the 5 local objects are expected; file3 blob, commit in A to add it
# and its tree, and 2 are our tree and the merge commit.
test_expect_success 'check objects expected to exist locally' '
	test_objcount D 5
'

test_expect_success 'preparing alternate repository #1' '
	test_create_repo F &&
	commit_in F file1
'

test_expect_success 'cloning alternate repo #2 and adding changes to repo #1' '
	git clone F G &&
	commit_in F file2
'

test_expect_success 'cloning alternate repo #1, using #2 as reference' '
	git clone --reference G F H
'

test_expect_success 'cloning with reference being subset of source (-l -s)' '
	git clone -l -s --reference A B E
'

test_expect_success 'cloning with multiple references drops duplicates' '
	git clone -s --reference B --reference A --reference B A dups &&
	test_line_count = 2 dups/.git/objects/info/alternates
'

test_expect_success 'clone with reference from a tagged repository' '
	(
		cd A && git tag -a -m tagged HEAD
	) &&
	git clone --reference=A A I
'

test_expect_success 'prepare branched repository' '
	git clone A J &&
	(
		cd J &&
		git checkout -b other master^ &&
		echo other >otherfile &&
		git add otherfile &&
		git commit -m other &&
		git checkout master
	)
'

test_expect_success 'fetch with incomplete alternates' '
	git init K &&
	echo "$base_dir/A/.git/objects" >K/.git/objects/info/alternates &&
	(
		cd K &&
		git remote add J "file://$base_dir/J" &&
		GIT_TRACE_PACKET=$U.K git fetch J
	) &&
	master_object=$(cd A && git for-each-ref --format="%(objectname)" refs/heads/master) &&
	test -s "$U.K" &&
	! grep " want $master_object" "$U.K" &&
	tag_object=$(cd A && git for-each-ref --format="%(objectname)" refs/tags/HEAD) &&
	! grep " want $tag_object" "$U.K"
'

test_expect_success 'clone using repo with gitfile as a reference' '
	git clone --separate-git-dir=L A M &&
	git clone --reference=M A N &&
	echo "$base_dir/L/objects" >expected &&
	test_cmp expected "$base_dir/N/.git/objects/info/alternates"
'

test_expect_success 'clone using repo pointed at by gitfile as reference' '
	git clone --reference=M/.git A O &&
	echo "$base_dir/L/objects" >expected &&
	test_cmp expected "$base_dir/O/.git/objects/info/alternates"
'

test_expect_success 'clone and dissociate from reference' '
	git init P &&
	(
		cd P &&	test_commit one
	) &&
	git clone P Q &&
	(
		cd Q && test_commit two
	) &&
	git clone --no-local --reference=P Q R &&
	git clone --no-local --reference=P --dissociate Q S &&
	# removing the reference P would corrupt R but not S
	rm -fr P &&
	test_must_fail git -C R fsck &&
	git -C S fsck
'
test_expect_success 'clone, dissociate from partial reference and repack' '
	rm -fr P Q R &&
	git init P &&
	(
		cd P &&
		test_commit one &&
		git repack &&
		test_commit two &&
		git repack
	) &&
	git clone --bare P Q &&
	(
		cd P &&
		git checkout -b second &&
		test_commit three &&
		git repack
	) &&
	git clone --bare --dissociate --reference=P Q R &&
	ls R/objects/pack/*.pack >packs.txt &&
	test_line_count = 1 packs.txt
'

test_expect_success 'clone, dissociate from alternates' '
	rm -fr A B C &&
	test_create_repo A &&
	commit_in A file1 &&
	git clone --reference=A A B &&
	test_line_count = 1 B/.git/objects/info/alternates &&
	git clone --local --dissociate B C &&
	! test -f C/.git/objects/info/alternates &&
	( cd C && git fsck )
'

test_done
