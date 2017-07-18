#!/bin/sh

test_description='merge-recursive backend test'

. ./test-lib.sh

#         A      <- create some files
#        / \
#       B   C    <- cause rename/delete conflicts between B and C
#      /     \
#     |\     /|
#     | D   E |
#     |  \ /  |
#     |   X   |
#     |  / \  |
#     | /   \ |
#     |/     \|
#     F       G  <- merge E into B, D into C
#      \     /
#       \   /
#        \ /
#         H      <- recursive merge crashes
#

# initialize
test_expect_success 'setup repo with criss-cross history' '
	mkdir data &&

	# create a bunch of files
	n=1 &&
	while test $n -le 10
	do
		echo $n > data/$n &&
		n=$(($n+1)) ||
		return 1
	done &&

	# check them in
	git add data &&
	git commit -m A &&
	git branch A &&

	# a file in one branch
	git checkout -b B A &&
	git rm data/9 &&
	git add data &&
	git commit -m B &&

	# with a branch off of it
	git branch D &&

	# put some commits on D
	git checkout D &&
	echo testD > data/testD &&
	git add data &&
	git commit -m D &&

	# back up to the top, create another branch and cause
	# a rename conflict with the file we deleted earlier
	git checkout -b C A &&
	git mv data/9 data/new-9 &&
	git add data &&
	git commit -m C &&

	# with a branch off of it
	git branch E &&

	# put a commit on E
	git checkout E &&
	echo testE > data/testE &&
	git add data &&
	git commit -m E &&

	# now, merge E into B
	git checkout B &&
	test_must_fail git merge E &&
	# force-resolve
	git add data &&
	git commit -m F &&
	git branch F &&

	# and merge D into C
	git checkout C &&
	test_must_fail git merge D &&
	# force-resolve
	git add data &&
	git commit -m G &&
	git branch G
'

test_expect_success 'recursive merge between F and G, causes segfault' '
	git merge F
'

test_done
