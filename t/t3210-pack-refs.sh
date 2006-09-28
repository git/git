#!/bin/sh
#
# Copyright (c) 2005 Amos Waterland
# Copyright (c) 2006 Christian Couder
#

test_description='git pack-refs should not change the branch semantic

This test runs git pack-refs and git show-ref and checks that the branch
semantic is still the same.
'
. ./test-lib.sh

test_expect_success \
    'prepare a trivial repository' \
    'echo Hello > A &&
     git-update-index --add A &&
     git-commit -m "Initial commit." &&
     HEAD=$(git-rev-parse --verify HEAD)'

SHA1=

test_expect_success \
    'see if git show-ref works as expected' \
    'git-branch a &&
     SHA1=$(< .git/refs/heads/a) &&
     echo "$SHA1 refs/heads/a" >expect &&
     git-show-ref a >result &&
     diff expect result'

test_expect_success \
    'see if a branch still exists when packed' \
    'git-branch b &&
     git-pack-refs &&
     rm .git/refs/heads/b &&
     echo "$SHA1 refs/heads/b" >expect &&
     git-show-ref b >result &&
     diff expect result'

# test_expect_failure \
#     'git branch c/d should barf if branch c exists' \
#     'git-branch c &&
#      git-pack-refs &&
#      rm .git/refs/heads/c &&
#      git-branch c/d'

test_expect_success \
    'see if a branch still exists after git pack-refs --prune' \
    'git-branch e &&
     git-pack-refs --prune &&
     echo "$SHA1 refs/heads/e" >expect &&
     git-show-ref e >result &&
     diff expect result'

test_expect_failure \
    'see if git pack-refs --prune remove ref files' \
    'git-branch f &&
     git-pack-refs --prune &&
     ls .git/refs/heads/f'

test_expect_success \
    'git branch g should work when git branch g/h has been deleted' \
    'git-branch g/h &&
     git-pack-refs --prune &&
     git-branch -d g/h &&
     git-branch g &&
     git-pack-refs &&
     git-branch -d g'

test_done
