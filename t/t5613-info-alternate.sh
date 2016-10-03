#!/bin/sh
#
# Copyright (C) 2006 Martin Waitz <tali@admingilde.org>
#

test_description='test transitive info/alternate entries'
. ./test-lib.sh

base_dir=$(pwd)

test_expect_success 'preparing first repository' \
'test_create_repo A && cd A &&
echo "Hello World" > file1 &&
git add file1 &&
git commit -m "Initial commit" file1 &&
git repack -a -d &&
git prune'

cd "$base_dir"

test_expect_success 'preparing second repository' \
'git clone -l -s A B && cd B &&
echo "foo bar" > file2 &&
git add file2 &&
git commit -m "next commit" file2 &&
git repack -a -d -l &&
git prune'

cd "$base_dir"

test_expect_success 'preparing third repository' \
'git clone -l -s B C && cd C &&
echo "Goodbye, cruel world" > file3 &&
git add file3 &&
git commit -m "one more" file3 &&
git repack -a -d -l &&
git prune'

cd "$base_dir"

test_expect_success 'creating too deep nesting' \
'git clone -l -s C D &&
git clone -l -s D E &&
git clone -l -s E F &&
git clone -l -s F G &&
git clone --bare -l -s G H'

test_expect_success 'invalidity of deepest repository' \
'cd H && {
	git fsck
	test $? -ne 0
}'

cd "$base_dir"

test_expect_success 'validity of third repository' \
'cd C &&
git fsck'

cd "$base_dir"

test_expect_success 'validity of fourth repository' \
'cd D &&
git fsck'

cd "$base_dir"

test_expect_success 'breaking of loops' \
'echo "$base_dir"/B/.git/objects >> "$base_dir"/A/.git/objects/info/alternates&&
cd C &&
git fsck'

cd "$base_dir"

test_expect_success 'that info/alternates is necessary' \
'cd C &&
rm -f .git/objects/info/alternates &&
! (git fsck)'

cd "$base_dir"

test_expect_success 'that relative alternate is possible for current dir' \
'cd C &&
echo "../../../B/.git/objects" > .git/objects/info/alternates &&
git fsck'

cd "$base_dir"

test_expect_success \
    'that relative alternate is only possible for current dir' '
    cd D &&
    ! (git fsck)
'

cd "$base_dir"

test_done
