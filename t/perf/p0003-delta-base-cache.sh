#!/bin/sh

test_description='Test operations that emphasize the delta base cache.

We look at both "log --raw", which should put only trees into the delta cache,
and "log -Sfoo --raw", which should look at both trees and blobs.

Any effects will be emphasized if the test repository is fully packed (loose
objects obviously do not use the delta base cache at all). It is also
emphasized if the pack has long delta chains (e.g., as produced by "gc
--aggressive"), though cache is still quite noticeable even with the default
depth of 50.

The setting of core.deltaBaseCacheLimit in the source repository is also
relevant (depending on the size of your test repo), so be sure it is consistent
between runs.
'
. ./perf-lib.sh

test_perf_large_repo

# puts mostly trees into the delta base cache
test_perf 'log --raw' '
	git log --raw >/dev/null
'

test_perf 'log -S' '
	git log --raw -Sfoo >/dev/null
'

test_done
