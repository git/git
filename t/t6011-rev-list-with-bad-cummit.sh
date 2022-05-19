#!/bin/sh

test_description='git rev-list should notice bad cummits'

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
   git cummit -m "first cummit" &&
   echo "bar" > bar &&
   git add bar &&
   git cummit -m "second cummit" &&
   echo "baz" > baz &&
   git add baz &&
   git cummit -m "third cummit" &&
   echo "foo again" >> foo &&
   git add foo &&
   git cummit -m "fourth cummit" &&
   git repack -a -f -d
   '

test_expect_success 'verify number of revisions' \
   '
   revs=$(git rev-list --all | wc -l) &&
   test $revs -eq 4 &&
   first_cummit=$(git rev-parse HEAD~3)
   '

test_expect_success 'corrupt second cummit object' \
   '
   perl -i.bak -pe "s/second cummit/socond cummit/" .git/objects/pack/*.pack &&
   test_must_fail git fsck --full
   '

test_expect_success 'rev-list should fail' '
	test_must_fail env GIT_TEST_cummit_GRAPH=0 git -c core.cummitGraph=false rev-list --all > /dev/null
'

test_expect_success 'git repack _MUST_ fail' \
   '
   test_must_fail git repack -a -f -d
   '

test_expect_success 'first cummit is still available' \
   '
   git log $first_cummit
   '

test_done

