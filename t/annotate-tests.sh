# This file isn't used as a test script directly, instead it is
# sourced from t8001-annotate.sh and t8001-blame.sh.

test_expect_success \
    'prepare reference tree' \
    'echo "1A quick brown fox jumps over the" >file &&
     echo "lazy dog" >>file &&
     git add file
     GIT_AUTHOR_NAME="A" git commit -a -m "Initial."'

test_expect_success \
    'check all lines blamed on A' \
    '[ $(git annotate file | awk "{print \$3}" | grep -c "A") == 2 ]'

test_expect_success \
    'Setup new lines blamed on B' \
    'echo "2A quick brown fox jumps over the" >>file &&
     echo "lazy dog" >> file &&
     GIT_AUTHOR_NAME="B" git commit -a -m "Second."'

test_expect_success \
    'Two lines blamed on A' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "A") == 2 ]'

test_expect_success \
    'Two lines blamed on B' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "B") == 2 ]'

test_expect_success \
    'merge-setup part 1' \
    'git checkout -b branch1 master &&
     echo "3A slow green fox jumps into the" >> file &&
     echo "well." >> file &&
     GIT_AUTHOR_NAME="B1" git commit -a -m "Branch1-1"'

test_expect_success \
    'Two lines blamed on A' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^A$") == 2 ]'

test_expect_success \
    'Two lines blamed on B' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B$") == 2 ]'

test_expect_success \
    'Two lines blamed on B1' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B1$") == 2 ]'

test_expect_success \
    'merge-setup part 2' \
    'git checkout -b branch2 master &&
     sed -e "s/2A quick brown/4A quick brown lazy dog/" < file > file.new &&
     mv file.new file &&
     GIT_AUTHOR_NAME="B2" git commit -a -m "Branch2-1"'

test_expect_success \
    'Two lines blamed on A' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^A$") == 2 ]'

test_expect_success \
    'One line blamed on B' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B$") == 1 ]'

test_expect_success \
    'One line blamed on B2' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B2$") == 1 ]'


test_expect_success \
    'merge-setup part 3' \
    'git pull . branch1'

test_expect_success \
    'Two lines blamed on A' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^A$") == 2 ]'

test_expect_success \
    'One line blamed on B' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B$") == 1 ]'

test_expect_success \
    'Two lines blamed on B1' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B1$") == 2 ]'

test_expect_success \
    'One line blamed on B2' \
    '[ $($PROG file | awk "{print \$3}" | grep -c "^B2$") == 1 ]'
