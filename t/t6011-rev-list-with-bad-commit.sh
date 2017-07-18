#!/bin/sh

test_description='git rev-list should notice bad commits'

. ./test-lib.sh

# Note:
# - compression level is set to zero to make "corruptions" easier to perform
# - reflog is disabled to avoid extra references which would twart the test

test_expect_success 'setup' \
   '
   git init &&
   git config core.compression 0 &&
   git config core.logallrefupdates false &&
   echo "foo" > foo &&
   git add foo &&
   git commit -m "first commit" &&
   echo "bar" > bar &&
   git add bar &&
   git commit -m "second commit" &&
   echo "baz" > baz &&
   git add baz &&
   git commit -m "third commit" &&
   echo "foo again" >> foo &&
   git add foo &&
   git commit -m "fourth commit" &&
   git repack -a -f -d
   '

test_expect_success 'verify number of revisions' \
   '
   revs=$(git rev-list --all | wc -l) &&
   test $revs -eq 4 &&
   first_commit=$(git rev-parse HEAD~3)
   '

test_expect_success 'corrupt second commit object' \
   '
   perl -i.bak -pe "s/second commit/socond commit/" .git/objects/pack/*.pack &&
   test_must_fail git fsck --full
   '

test_expect_success 'rev-list should fail' \
   '
   test_must_fail git rev-list --all > /dev/null
   '

test_expect_success 'git repack _MUST_ fail' \
   '
   test_must_fail git repack -a -f -d
   '

test_expect_success 'first commit is still available' \
   '
   git log $first_commit
   '

test_done

