#!/bin/sh

test_description='but rev-list should notice bad cummits'

. ./test-lib.sh

# Note:
# - compression level is set to zero to make "corruptions" easier to perform
# - reflog is disabled to avoid extra references which would twart the test

test_expect_success 'setup' \
   '
   but init &&
   but config core.compression 0 &&
   but config core.logallrefupdates false &&
   echo "foo" > foo &&
   but add foo &&
   but cummit -m "first cummit" &&
   echo "bar" > bar &&
   but add bar &&
   but cummit -m "second cummit" &&
   echo "baz" > baz &&
   but add baz &&
   but cummit -m "third cummit" &&
   echo "foo again" >> foo &&
   but add foo &&
   but cummit -m "fourth cummit" &&
   but repack -a -f -d
   '

test_expect_success 'verify number of revisions' \
   '
   revs=$(but rev-list --all | wc -l) &&
   test $revs -eq 4 &&
   first_cummit=$(but rev-parse HEAD~3)
   '

test_expect_success 'corrupt second cummit object' \
   '
   perl -i.bak -pe "s/second cummit/socond cummit/" .but/objects/pack/*.pack &&
   test_must_fail but fsck --full
   '

test_expect_success 'rev-list should fail' '
	test_must_fail env BUT_TEST_CUMMIT_GRAPH=0 but -c core.cummitGraph=false rev-list --all > /dev/null
'

test_expect_success 'but repack _MUST_ fail' \
   '
   test_must_fail but repack -a -f -d
   '

test_expect_success 'first cummit is still available' \
   '
   but log $first_cummit
   '

test_done

