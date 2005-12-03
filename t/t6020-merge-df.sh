#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

test_description='Test merge with directory/file conflicts'
. ./test-lib.sh

test_expect_success 'prepare repository' \
'echo "Hello" > init &&
git add init &&
git commit -m "Initial commit" &&
git branch B &&
mkdir dir &&
echo "foo" > dir/foo &&
git add dir/foo &&
git commit -m "File: dir/foo" &&
git checkout B &&
echo "file dir" > dir &&
git add dir &&
git commit -m "File: dir"'

test_expect_code 1 'Merge with d/f conflicts' 'git merge "merge msg" B master'

test_done
