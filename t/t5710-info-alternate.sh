#!/bin/sh
#
# Copyright (C) 2006 Martin Waitz <tali@admingilde.org>
#

test_description='test transitive info/alternate entries'
. ./test-lib.sh

# test that a file is not reachable in the current repository
# but that it is after creating a info/alternate entry
reachable_via() {
	alternate="$1"
	file="$2"
	if git cat-file -e "HEAD:$file"; then return 1; fi
	echo "$alternate" >> .git/objects/info/alternate
	git cat-file -e "HEAD:$file"
}

test_valid_repo() {
	git fsck-objects --full > fsck.log &&
	test `wc -l < fsck.log` = 0
}

base_dir=`pwd`

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

test_expect_failure 'creating too deep nesting' \
'git clone -l -s C D &&
git clone -l -s D E &&
git clone -l -s E F &&
git clone -l -s F G &&
git clone -l -s G H &&
cd H &&
test_valid_repo'

cd "$base_dir"

test_expect_success 'validity of third repository' \
'cd C &&
test_valid_repo'

cd "$base_dir"

test_expect_success 'validity of fourth repository' \
'cd D &&
test_valid_repo'

cd "$base_dir"

test_expect_success 'breaking of loops' \
"echo '$base_dir/B/.git/objects' >> '$base_dir'/A/.git/objects/info/alternates&&
cd C &&
test_valid_repo"

cd "$base_dir"

test_expect_failure 'that info/alternates is necessary' \
'cd C &&
rm .git/objects/info/alternates &&
test_valid_repo'

cd "$base_dir"

test_expect_success 'that relative alternate is possible for current dir' \
'cd C &&
echo "../../../B/.git/objects" > .git/objects/info/alternates &&
test_valid_repo'

cd "$base_dir"

test_expect_failure 'that relative alternate is only possible for current dir' \
'cd D &&
test_valid_repo'

cd "$base_dir"

test_done

