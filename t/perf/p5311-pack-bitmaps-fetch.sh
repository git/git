#!/bin/sh

test_description='performance of fetches from bitmapped packs'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'create bitmapped server repo' '
	git config pack.writebitmaps true &&
	git repack -ad
'

# simulate a fetch from a repository that last fetched N days ago, for
# various values of N. We do so by following the first-parent chain,
# and assume the first entry in the chain that is N days older than the current
# HEAD is where the HEAD would have been then.
for days in 1 2 4 8 16 32 64 128; do
	title=$(printf '%10s' "($days days)")
	test_expect_success "setup revs from $days days ago" '
		now=$(git log -1 --format=%ct HEAD) &&
		then=$(($now - ($days * 86400))) &&
		tip=$(git rev-list -1 --first-parent --until=$then HEAD) &&
		{
			echo HEAD &&
			echo ^$tip
		} >revs
	'

	test_perf "server $title" '
		git pack-objects --stdout --revs \
				 --thin --delta-base-offset \
				 <revs >tmp.pack
	'

	test_size "size   $title" '
		wc -c <tmp.pack
	'

	test_perf "client $title" '
		git index-pack --stdin --fix-thin <tmp.pack
	'
done

test_done
