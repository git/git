#!/bin/sh
#
# Copyright (C) 2006 Martin Waitz <tali@admingilde.org>
#

test_description='test clone --reference'
. ./test-lib.sh

base_dir=`pwd`

U=$base_dir/UPLOAD_LOG

test_expect_success 'preparing first repository' \
'test_create_repo A && cd A &&
echo first > file1 &&
git add file1 &&
git commit -m initial'

cd "$base_dir"

test_expect_success 'preparing second repository' \
'git clone A B && cd B &&
echo second > file2 &&
git add file2 &&
git commit -m addition &&
git repack -a -d &&
git prune'

cd "$base_dir"

test_expect_success 'cloning with reference (-l -s)' \
'git clone -l -s --reference B A C'

cd "$base_dir"

test_expect_success 'existence of info/alternates' \
'test `wc -l <C/.git/objects/info/alternates` = 2'

cd "$base_dir"

test_expect_success 'pulling from reference' \
'cd C &&
git pull ../B master'

cd "$base_dir"

test_expect_success 'that reference gets used' \
'cd C &&
echo "0 objects, 0 kilobytes" > expected &&
git count-objects > current &&
test_cmp expected current'

cd "$base_dir"

rm -f "$U"

test_expect_success 'cloning with reference (no -l -s)' \
'GIT_DEBUG_SEND_PACK=3 git clone --reference B "file://$(pwd)/A" D 3>"$U"'

test_expect_success 'fetched no objects' \
'! grep "^want" "$U"'

cd "$base_dir"

test_expect_success 'existence of info/alternates' \
'test `wc -l <D/.git/objects/info/alternates` = 1'

cd "$base_dir"

test_expect_success 'pulling from reference' \
'cd D && git pull ../B master'

cd "$base_dir"

test_expect_success 'that reference gets used' \
'cd D && echo "0 objects, 0 kilobytes" > expected &&
git count-objects > current &&
test_cmp expected current'

cd "$base_dir"

test_expect_success 'updating origin' \
'cd A &&
echo third > file3 &&
git add file3 &&
git commit -m update &&
git repack -a -d &&
git prune'

cd "$base_dir"

test_expect_success 'pulling changes from origin' \
'cd C &&
git pull origin'

cd "$base_dir"

# the 2 local objects are commit and tree from the merge
test_expect_success 'that alternate to origin gets used' \
'cd C &&
echo "2 objects" > expected &&
git count-objects | cut -d, -f1 > current &&
test_cmp expected current'

cd "$base_dir"

test_expect_success 'pulling changes from origin' \
'cd D &&
git pull origin'

cd "$base_dir"

# the 5 local objects are expected; file3 blob, commit in A to add it
# and its tree, and 2 are our tree and the merge commit.
test_expect_success 'check objects expected to exist locally' \
'cd D &&
echo "5 objects" > expected &&
git count-objects | cut -d, -f1 > current &&
test_cmp expected current'

cd "$base_dir"

test_expect_success 'preparing alternate repository #1' \
'test_create_repo F && cd F &&
echo first > file1 &&
git add file1 &&
git commit -m initial'

cd "$base_dir"

test_expect_success 'cloning alternate repo #2 and adding changes to repo #1' \
'git clone F G && cd F &&
echo second > file2 &&
git add file2 &&
git commit -m addition'

cd "$base_dir"

test_expect_success 'cloning alternate repo #1, using #2 as reference' \
'git clone --reference G F H'

cd "$base_dir"

test_expect_success 'cloning with reference being subset of source (-l -s)' \
'git clone -l -s --reference A B E'

cd "$base_dir"

test_done
