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
	but add data &&
	but cummit -m A &&
	but branch A &&

	# a file in one branch
	but checkout -b B A &&
	but rm data/9 &&
	but add data &&
	but cummit -m B &&

	# with a branch off of it
	but branch D &&

	# put some cummits on D
	but checkout D &&
	echo testD > data/testD &&
	but add data &&
	but cummit -m D &&

	# back up to the top, create another branch and cause
	# a rename conflict with the file we deleted earlier
	but checkout -b C A &&
	but mv data/9 data/new-9 &&
	but add data &&
	but cummit -m C &&

	# with a branch off of it
	but branch E &&

	# put a cummit on E
	but checkout E &&
	echo testE > data/testE &&
	but add data &&
	but cummit -m E &&

	# now, merge E into B
	but checkout B &&
	test_must_fail but merge E &&
	# force-resolve
	but add data &&
	but cummit -m F &&
	but branch F &&

	# and merge D into C
	but checkout C &&
	test_must_fail but merge D &&
	# force-resolve
	but add data &&
	but cummit -m G &&
	but branch G
'

test_expect_success 'recursive merge between F and G does not cause segfault' '
	but merge F
'

test_done
