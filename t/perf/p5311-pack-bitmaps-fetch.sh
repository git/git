#!/bin/sh

test_description='performance of fetches from bitmapped packs'
. ./perf-lib.sh

test_fetch_bitmaps () {
	argv=$1
	export argv

	test_expect_success 'setup test directory' '
		rm -fr * .git
	'

	test_perf_default_repo

	test_expect_success "create bitmapped server repo ${argv:+($argv)}" '
		git config pack.writebitmaps true &&
		git repack -adF $argv
	'

	test_size "size of bitmapped pack ${argv:+($argv)}" '
		test_file_size .git/objects/pack/pack-*.pack
	'

	# simulate a fetch from a repository that last fetched N days ago, for
	# various values of N. We do so by following the first-parent chain,
	# and assume the first entry in the chain that is N days older than the current
	# HEAD is where the HEAD would have been then.
	for days in 1 2 4 8 16 32 64 128; do
		title=$(printf '%10s' "($days days${argv:+, $argv})")
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
			test_file_size tmp.pack
		'

		test_perf "client $title" '
			git index-pack --stdin --fix-thin <tmp.pack
		'
	done
}

for argv in '' --path-walk
do
	test_fetch_bitmaps $argv || return 1
done

test_done
