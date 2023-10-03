#!/bin/sh

test_description='git repack works correctly'

. ./test-lib.sh

objdir=.git/objects

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
		cat expired.objects remaining.objects | sort >actual &&
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

test_done
