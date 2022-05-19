#!/bin/sh

test_description='check bitmap operation with shallow repositories'
. ./test-lib.sh

# We want to create a situation where the shallow, grafted
# view of reachability does not match reality in a way that
# might cause us to send insufficient objects.
#
# We do this with a history that repeats a state, like:
#
#      A    --   B    --   C
#    file=1    file=2    file=1
#
# and then create a shallow clone to the second cummit, B.
# In a non-shallow clone, that would mean we already have
# the tree for A. But in a shallow one, we've grafted away
# A, and fetching A to B requires that the other side send
# us the tree for file=1.
test_expect_success 'setup shallow repo' '
	echo 1 >file &&
	but add file &&
	but cummit -m orig &&
	echo 2 >file &&
	but cummit -a -m update &&
	but clone --no-local --bare --depth=1 . shallow.but &&
	echo 1 >file &&
	but cummit -a -m repeat
'

test_expect_success 'turn on bitmaps in the parent' '
	but repack -adb
'

test_expect_success 'shallow fetch from bitmapped repo' '
	(cd shallow.but && but fetch)
'

test_done
