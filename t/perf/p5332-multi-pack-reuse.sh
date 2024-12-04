#!/bin/sh

test_description='tests pack performance with multi-pack reuse'

. ./perf-lib.sh
. "${TEST_DIRECTORY}/perf/lib-pack.sh"

packdir=.git/objects/pack

test_perf_large_repo

find_pack () {
	for idx in $packdir/pack-*.idx
	do
		if git show-index <$idx | grep -q "$1"
		then
			basename $idx
		fi || return 1
	done
}

repack_into_n_chunks () {
	git repack -adk &&

	test "$1" -eq 1 && return ||

	find $packdir -type f | sort >packs.before &&

	# partition the repository into $1 chunks of consecutive commits, and
	# then create $1 packs with the objects reachable from each chunk
	# (excluding any objects reachable from the previous chunks)
	sz="$(($(git rev-list --count --all) / $1))"
	for rev in $(git rev-list --all | awk "NR % $sz == 0" | tac)
	do
		pack="$(echo "$rev" | git pack-objects --revs \
			--honor-pack-keep --delta-base-offset $packdir/pack)" &&
		touch $packdir/pack-$pack.keep || return 1
	done

	# grab any remaining objects not packed by the previous step(s)
	git pack-objects --revs --all --honor-pack-keep --delta-base-offset \
		$packdir/pack &&

	find $packdir -type f | sort >packs.after &&

	# and install the whole thing
	for f in $(comm -12 packs.before packs.after)
	do
		rm -f "$f" || return 1
	done
	rm -fr $packdir/*.keep
}

for nr_packs in 1 10 100
do
	test_expect_success "create $nr_packs-pack scenario" '
		repack_into_n_chunks $nr_packs
	'

	test_expect_success "setup bitmaps for $nr_packs-pack scenario" '
		find $packdir -type f -name "*.idx" | sed -e "s/.*\/\(.*\)$/+\1/g" |
		git multi-pack-index write --stdin-packs --bitmap \
			--preferred-pack="$(find_pack $(git rev-parse HEAD))"
	'

	for reuse in single multi
	do
		test_perf "clone for $nr_packs-pack scenario ($reuse-pack reuse)" "
			git for-each-ref --format='%(objectname)' refs/heads refs/tags >in &&
			git -c pack.allowPackReuse=$reuse pack-objects \
				--revs --delta-base-offset --use-bitmap-index \
				--stdout <in >result
		"

		test_size "clone size for $nr_packs-pack scenario ($reuse-pack reuse)" '
			test_file_size result
		'
	done
done

test_done
