#!/bin/sh

test_description='git repack works correctly'

. ./test-lib.sh

objdir=.git/objects
packdir=$objdir/pack

test_expect_success '--expire-to stores pruned objects (now)' '
	git init expire-to-now &&
	(
		cd expire-to-now &&

		git branch -M main &&

		test_commit base &&

		git checkout -b cruft &&
		test_commit --no-tag cruft &&

		git rev-list --objects --no-object-names main..cruft >moved.raw &&
		sort moved.raw >moved.want &&

		git rev-list --all --objects --no-object-names >expect.raw &&
		sort expect.raw >expect &&

		git checkout main &&
		git branch -D cruft &&
		git reflog expire --all --expire=all &&

		git init --bare expired.git &&
		git repack -d \
			--cruft --cruft-expiration="now" \
			--expire-to="expired.git/objects/pack/pack" &&

		expired="$(ls expired.git/objects/pack/pack-*.idx)" &&
		test_path_is_file "${expired%.idx}.mtimes" &&

		# Since the `--cruft-expiration` is "now", the effective
		# behavior is to move _all_ unreachable objects out to
		# the location in `--expire-to`.
		git show-index <$expired >expired.raw &&
		cut -d" " -f2 expired.raw | sort >expired.objects &&
		git rev-list --all --objects --no-object-names \
			>remaining.objects &&

		# ...in other words, the combined contents of this
		# repository and expired.git should be the same as the
		# set of objects we started with.
		sort expired.objects remaining.objects >actual &&
		test_cmp expect actual &&

		# The "moved" objects (i.e., those in expired.git)
		# should be the same as the cruft objects which were
		# expired in the previous step.
		test_cmp moved.want expired.objects
	)
'

test_expect_success '--expire-to stores pruned objects (5.minutes.ago)' '
	git init expire-to-5.minutes.ago &&
	(
		cd expire-to-5.minutes.ago &&

		git branch -M main &&

		test_commit base &&

		# Create two classes of unreachable objects, one which
		# is older than 5 minutes (stale), and another which is
		# newer (recent).
		for kind in stale recent
		do
			git checkout -b $kind main &&
			test_commit --no-tag $kind || return 1
		done &&

		git rev-list --objects --no-object-names main..stale >in &&
		stale="$(git pack-objects $objdir/pack/pack <in)" &&
		mtime="$(test-tool chmtime --get =-600 $objdir/pack/pack-$stale.pack)" &&

		# expect holds the set of objects we expect to find in
		# this repository after repacking
		git rev-list --objects --no-object-names recent >expect.raw &&
		sort expect.raw >expect &&

		# moved.want holds the set of objects we expect to find
		# in expired.git
		git rev-list --objects --no-object-names main..stale >out &&
		sort out >moved.want &&

		git checkout main &&
		git branch -D stale recent &&
		git reflog expire --all --expire=all &&
		git prune-packed &&

		git init --bare expired.git &&
		git repack -d \
			--cruft --cruft-expiration=5.minutes.ago \
			--expire-to="expired.git/objects/pack/pack" &&

		# Some of the remaining objects in this repository are
		# unreachable, so use `cat-file --batch-all-objects`
		# instead of `rev-list` to get their names
		git cat-file --batch-all-objects --batch-check="%(objectname)" \
			>remaining.objects &&
		sort remaining.objects >actual &&
		test_cmp expect actual &&

		(
			cd expired.git &&

			expired="$(ls objects/pack/pack-*.mtimes)" &&
			test-tool pack-mtimes $(basename $expired) >out &&
			cut -d" " -f1 out | sort >../moved.got &&

			# Ensure that there are as many objects with the
			# expected mtime as were moved to expired.git.
			#
			# In other words, ensure that the recorded
			# mtimes of any moved objects was written
			# correctly.
			grep " $mtime$" out >matching &&
			test_line_count = $(wc -l <../moved.want) matching
		) &&
		test_cmp moved.want moved.got
	)
'

generate_random_blob() {
	test-tool genrandom "$@" >blob &&
	git hash-object -w -t blob blob &&
	rm blob
}

pack_random_blob () {
	generate_random_blob "$@" &&
	git repack -d -q >/dev/null
}

generate_cruft_pack () {
	pack_random_blob "$@" >/dev/null &&

	ls $packdir/pack-*.pack | xargs -n 1 basename >in &&
	pack="$(git pack-objects --cruft $packdir/pack <in)" &&
	git prune-packed &&

	echo "$packdir/pack-$pack.mtimes"
}

test_expect_success '--max-cruft-size creates new packs when above threshold' '
	git init max-cruft-size-large &&
	(
		cd max-cruft-size-large &&
		test_commit base &&

		foo="$(pack_random_blob foo $((1*1024*1024)))" &&
		git repack --cruft -d &&
		cruft_foo="$(ls $packdir/pack-*.mtimes)" &&

		bar="$(pack_random_blob bar $((1*1024*1024)))" &&
		git repack --cruft -d --max-cruft-size=1M &&
		cruft_bar="$(ls $packdir/pack-*.mtimes | grep -v $cruft_foo)" &&

		test-tool pack-mtimes $(basename "$cruft_foo") >foo.objects &&
		test-tool pack-mtimes $(basename "$cruft_bar") >bar.objects &&

		grep "^$foo" foo.objects &&
		test_line_count = 1 foo.objects &&
		grep "^$bar" bar.objects &&
		test_line_count = 1 bar.objects
	)
'

test_expect_success '--max-cruft-size combines existing packs when below threshold' '
	git init max-cruft-size-small &&
	(
		cd max-cruft-size-small &&
		test_commit base &&

		foo="$(pack_random_blob foo $((1*1024*1024)))" &&
		git repack --cruft -d &&

		bar="$(pack_random_blob bar $((1*1024*1024)))" &&
		git repack --cruft -d --max-cruft-size=10M &&

		cruft=$(ls $packdir/pack-*.mtimes) &&
		test-tool pack-mtimes $(basename "$cruft") >cruft.objects &&

		grep "^$foo" cruft.objects &&
		grep "^$bar" cruft.objects &&
		test_line_count = 2 cruft.objects
	)
'

test_expect_success '--max-cruft-size combines smaller packs first' '
	git init max-cruft-size-consume-small &&
	(
		cd max-cruft-size-consume-small &&

		test_commit base &&
		git repack -ad &&

		cruft_foo="$(generate_cruft_pack foo 524288)" &&    # 0.5 MiB
		cruft_bar="$(generate_cruft_pack bar 524288)" &&    # 0.5 MiB
		cruft_baz="$(generate_cruft_pack baz 1048576)" &&   # 1.0 MiB
		cruft_quux="$(generate_cruft_pack quux 1572864)" && # 1.5 MiB

		test-tool pack-mtimes "$(basename $cruft_foo)" >expect.raw &&
		test-tool pack-mtimes "$(basename $cruft_bar)" >>expect.raw &&
		sort expect.raw >expect.objects &&

		# repacking with `--max-cruft-size=2M` should combine
		# both 0.5 MiB packs together, instead of, say, one of
		# the 0.5 MiB packs with the 1.0 MiB pack
		ls $packdir/pack-*.mtimes | sort >cruft.before &&
		git repack -d --cruft --max-cruft-size=2M &&
		ls $packdir/pack-*.mtimes | sort >cruft.after &&

		comm -13 cruft.before cruft.after >cruft.new &&
		comm -23 cruft.before cruft.after >cruft.removed &&

		test_line_count = 1 cruft.new &&
		test_line_count = 2 cruft.removed &&

		# the two smaller packs should be rolled up first
		printf "%s\n" $cruft_foo $cruft_bar | sort >expect.removed &&
		test_cmp expect.removed cruft.removed &&

		# ...and contain the set of objects rolled up
		test-tool pack-mtimes "$(basename $(cat cruft.new))" >actual.raw &&
		sort actual.raw >actual.objects &&

		test_cmp expect.objects actual.objects
	)
'

test_expect_success 'setup --max-cruft-size with freshened objects' '
	git init max-cruft-size-freshen &&
	(
		cd max-cruft-size-freshen &&

		test_commit base &&
		git repack -ad &&

		foo="$(generate_random_blob foo 64)" &&
		test-tool chmtime --get -10000 \
			"$objdir/$(test_oid_to_path "$foo")" >foo.mtime &&

		git repack --cruft -d &&

		cruft="$(ls $packdir/pack-*.mtimes)" &&
		test-tool pack-mtimes "$(basename $cruft)" >actual &&
		echo "$foo $(cat foo.mtime)" >expect &&
		test_cmp expect actual
	)
'

test_expect_success '--max-cruft-size with freshened objects (loose)' '
	(
		cd max-cruft-size-freshen &&

		# regenerate the object, setting its mtime to be more recent
		foo="$(generate_random_blob foo 64)" &&
		test-tool chmtime --get -100 \
			"$objdir/$(test_oid_to_path "$foo")" >foo.mtime &&

		git repack --cruft -d &&

		cruft="$(ls $packdir/pack-*.mtimes)" &&
		test-tool pack-mtimes "$(basename $cruft)" >actual &&
		echo "$foo $(cat foo.mtime)" >expect &&
		test_cmp expect actual
	)
'

test_expect_success '--max-cruft-size with freshened objects (packed)' '
	(
		cd max-cruft-size-freshen &&

		# regenerate the object and store it in a packfile,
		# setting its mtime to be more recent
		#
		# store it alongside another cruft object so that we
		# do not create an identical copy of the existing
		# cruft pack (which contains $foo).
		foo="$(generate_random_blob foo 64)" &&
		bar="$(generate_random_blob bar 64)" &&
		foo_pack="$(printf "%s\n" $foo $bar | git pack-objects $packdir/pack)" &&
		git prune-packed &&

		test-tool chmtime --get -10 \
			"$packdir/pack-$foo_pack.pack" >foo.mtime &&

		git repack --cruft -d &&

		cruft="$(ls $packdir/pack-*.mtimes)" &&
		test-tool pack-mtimes "$(basename $cruft)" >actual &&
		echo "$foo $(cat foo.mtime)" >expect.raw &&
		echo "$bar $(cat foo.mtime)" >>expect.raw &&
		sort expect.raw >expect &&
		test_cmp expect actual
	)
'

test_expect_success '--max-cruft-size with pruning' '
	git init max-cruft-size-prune &&
	(
		cd max-cruft-size-prune &&

		test_commit base &&
		foo="$(generate_random_blob foo $((1024*1024)))" &&
		bar="$(generate_random_blob bar $((1024*1024)))" &&
		baz="$(generate_random_blob baz $((1024*1024)))" &&

		test-tool chmtime -10000 "$objdir/$(test_oid_to_path "$foo")" &&

		git repack -d --cruft --max-cruft-size=1M &&

		# backdate the mtimes of all cruft packs to validate
		# that they were rewritten as a result of pruning
		ls $packdir/pack-*.mtimes | sort >cruft.before &&
		for cruft in $(cat cruft.before)
		do
			mtime="$(test-tool chmtime --get -10000 "$cruft")" &&
			echo $cruft $mtime >>mtimes || return 1
		done &&

		# repack (and prune) with a --max-cruft-size to ensure
		# that we appropriately split the resulting set of packs
		git repack -d --cruft --max-cruft-size=1M \
			--cruft-expiration=10.seconds.ago &&
		ls $packdir/pack-*.mtimes | sort >cruft.after &&

		for cruft in $(cat cruft.after)
		do
			old_mtime="$(grep $cruft mtimes | cut -d" " -f2)" &&
			new_mtime="$(test-tool chmtime --get $cruft)" &&
			test $old_mtime -lt $new_mtime || return 1
		done &&

		test_line_count = 3 cruft.before &&
		test_line_count = 2 cruft.after &&
		test_must_fail git cat-file -e $foo &&
		git cat-file -e $bar &&
		git cat-file -e $baz
	)
'

test_expect_success '--max-cruft-size ignores non-local packs' '
	repo="max-cruft-size-non-local" &&
	git init $repo &&
	(
		cd $repo &&
		test_commit base &&
		generate_random_blob foo 64 &&
		git repack --cruft -d
	) &&

	git clone --reference=$repo $repo $repo-alt &&
	(
		cd $repo-alt &&

		test_commit other &&
		generate_random_blob bar 64 &&

		# ensure that we do not attempt to pick up packs from
		# the non-alternated repository, which would result in a
		# crash
		git repack --cruft --max-cruft-size=1M -d
	)
'

test_expect_success 'reachable packs are preferred over cruft ones' '
	repo="cruft-preferred-packs" &&
	git init "$repo" &&
	(
		cd "$repo" &&

		# This test needs to exercise careful control over when a MIDX
		# is and is not written. Unset the corresponding TEST variable
		# accordingly.
		sane_unset GIT_TEST_MULTI_PACK_INDEX &&

		test_commit base &&
		test_commit --no-tag cruft &&

		non_cruft="$(echo base | git pack-objects --revs $packdir/pack)" &&
		# Write a cruft pack which both (a) sorts ahead of the non-cruft
		# pack in lexical order, and (b) has an older mtime to appease
		# the MIDX preferred pack selection routine.
		cruft="$(echo pack-$non_cruft.pack | git pack-objects --cruft $packdir/pack-A)" &&
		test-tool chmtime -1000 $packdir/pack-A-$cruft.pack &&

		test_commit other &&
		git repack -d &&

		git repack --geometric 2 -d --write-midx --write-bitmap-index &&

		# After repacking, there are two packs left: one reachable one
		# (which is the result of combining both of the existing two
		# non-cruft packs), and one cruft pack.
		find .git/objects/pack -type f -name "*.pack" >packs &&
		test_line_count = 2 packs &&

		# Make sure that the pack we just wrote is marked as preferred,
		# not the cruft one.
		pack="$(test-tool read-midx --preferred-pack $objdir)" &&
		test_path_is_missing "$packdir/$(basename "$pack" ".idx").mtimes"
	)
'

test_done
