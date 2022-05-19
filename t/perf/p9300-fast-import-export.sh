#!/bin/sh

test_description='test fast-import and fast-export performance'
. ./perf-lib.sh

test_perf_default_repo

# Use --no-data here to produce a vastly smaller export file.
# This is much cheaper to work with but should still exercise
# fast-import pretty well (we'll still process all cummits and
# trees, which account for 60% or more of objects in most repos).
#
# Use --reencode to avoid the default of aborting on non-utf8 cummits,
# which lets this test run against a wider variety of sample repos.
test_perf 'export (no-blobs)' '
	but fast-export --reencode=yes --no-data HEAD >export
'

test_perf 'import (no-blobs)' '
	but fast-import --force <export
'

test_done
