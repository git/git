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

echo '[core] logallrefupdates = true' >>.git/config

test_expect_success \
    'prepare a trivial repository' \
    'echo Hello > A &&
     git update-index --add A &&
     git commit -m "Initial commit." &&
     HEAD=$(git rev-parse --verify HEAD)'

SHA1=

test_expect_success \
    'see if git show-ref works as expected' \
    'git branch a &&
     SHA1=`cat .git/refs/heads/a` &&
     echo "$SHA1 refs/heads/a" >expect &&
     git show-ref a >result &&
     test_cmp expect result'

test_expect_success \
    'see if a branch still exists when packed' \
    'git branch b &&
     git pack-refs --all &&
     rm -f .git/refs/heads/b &&
     echo "$SHA1 refs/heads/b" >expect &&
     git show-ref b >result &&
     test_cmp expect result'

test_expect_success 'git branch c/d should barf if branch c exists' '
     git branch c &&
     git pack-refs --all &&
     rm -f .git/refs/heads/c &&
     test_must_fail git branch c/d
'

test_expect_success \
    'see if a branch still exists after git pack-refs --prune' \
    'git branch e &&
     git pack-refs --all --prune &&
     echo "$SHA1 refs/heads/e" >expect &&
     git show-ref e >result &&
     test_cmp expect result'

test_expect_success 'see if git pack-refs --prune remove ref files' '
     git branch f &&
     git pack-refs --all --prune &&
     ! test -f .git/refs/heads/f
'

test_expect_success 'see if git pack-refs --prune removes empty dirs' '
     git branch r/s/t &&
     git pack-refs --all --prune &&
     ! test -e .git/refs/heads/r
'

test_expect_success \
    'git branch g should work when git branch g/h has been deleted' \
    'git branch g/h &&
     git pack-refs --all --prune &&
     git branch -d g/h &&
     git branch g &&
     git pack-refs --all &&
     git branch -d g'

test_expect_success 'git branch i/j/k should barf if branch i exists' '
     git branch i &&
     git pack-refs --all --prune &&
     test_must_fail git branch i/j/k
'

test_expect_success \
    'test git branch k after branch k/l/m and k/lm have been deleted' \
    'git branch k/l &&
     git branch k/lm &&
     git branch -d k/l &&
     git branch k/l/m &&
     git branch -d k/l/m &&
     git branch -d k/lm &&
     git branch k'

test_expect_success \
    'test git branch n after some branch deletion and pruning' \
    'git branch n/o &&
     git branch n/op &&
     git branch -d n/o &&
     git branch n/o/p &&
     git branch -d n/op &&
     git pack-refs --all --prune &&
     git branch -d n/o/p &&
     git branch n'

test_expect_success \
	'see if up-to-date packed refs are preserved' \
	'git branch q &&
	 git pack-refs --all --prune &&
	 git update-ref refs/heads/q refs/heads/q &&
	 ! test -f .git/refs/heads/q'

test_expect_success 'pack, prune and repack' '
	git tag foo &&
	git pack-refs --all --prune &&
	git show-ref >all-of-them &&
	git pack-refs &&
	git show-ref >again &&
	test_cmp all-of-them again
'

test_done
