#!/bin/sh
#
# Copyright (c) 2005 Fredrik Kuivinen
#

# See http://marc.info/?l=git&m=111463358500362&w=2 for a
# nice description of what this is about.


test_description='Test criss-cross merge'
. ./test-lib.sh

test_expect_success 'prepare repository' \
'echo "1
2
3
4
5
6
7
8
9" > file &&
git add file &&
git commit -m "Initial commit" file &&
git branch A &&
git branch B &&
git checkout A &&
echo "1
2
3
4
5
6
7
8 changed in B8, branch A
9" > file &&
git commit -m "B8" file &&
git checkout B &&
echo "1
2
3 changed in C3, branch B
4
5
6
7
8
9
" > file &&
git commit -m "C3" file &&
git branch C3 &&
git merge "pre E3 merge" B A &&
echo "1
2
3 changed in E3, branch B. New file size
4
5
6
7
8 changed in B8, branch A
9
" > file &&
git commit -m "E3" file &&
git checkout A &&
git merge "pre D8 merge" A C3 &&
echo "1
2
3 changed in C3, branch B
4
5
6
7
8 changed in D8, branch A. New file size 2
9" > file &&
git commit -m D8 file'

test_expect_success 'Criss-cross merge' 'git merge "final merge" A B'

cat > file-expect <<EOF
1
2
3 changed in E3, branch B. New file size
4
5
6
7
8 changed in D8, branch A. New file size 2
9
EOF

test_expect_success 'Criss-cross merge result' 'cmp file file-expect'

test_expect_success 'Criss-cross merge fails (-s resolve)' \
'git reset --hard A^ &&
test_must_fail git merge -s resolve -m "final merge" B'

test_done
