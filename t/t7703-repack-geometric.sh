#!/bin/sh

test_description='git repack --geometric works correctly'

. ./test-lib.sh

GIT_TEST_MULTI_PACK_INDEX=0

objdir=.git/objects
packdir=$objdir/pack
midx=$objdir/pack/multi-pack-index

test_expect_success '--geometric with no packs' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		git repack --write-midx --geometric 2 >out &&
		test_i18ngrep "Nothing new to pack" out
	)
'

test_expect_success '--geometric with one pack' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		test_commit "base" &&
		git repack -d &&

		git repack --geometric 2 >out &&

		test_i18ngrep "Nothing new to pack" out
	)
'

test_expect_success '--geometric with an intact progression' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		# These packs already form a geometric progression.
		test_commit_bulk --start=1 1 && # 3 objects
		test_commit_bulk --start=2 2 && # 6 objects
		test_commit_bulk --start=4 4 && # 12 objects

		find $objdir/pack -name "*.pack" | sort >expect &&
		git repack --geometric 2 -d &&
		find $objdir/pack -name "*.pack" | sort >actual &&

		test_cmp expect actual
	)
'

test_expect_success '--geometric with loose objects' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		# These packs already form a geometric progression.
		test_commit_bulk --start=1 1 && # 3 objects
		test_commit_bulk --start=2 2 && # 6 objects
		# The loose objects are packed together, breaking the
		# progression.
		test_commit loose && # 3 objects

		find $objdir/pack -name "*.pack" | sort >before &&
		git repack --geometric 2 -d &&
		find $objdir/pack -name "*.pack" | sort >after &&

		comm -13 before after >new &&
		comm -23 before after >removed &&

		test_line_count = 1 new &&
		test_must_be_empty removed &&

		git repack --geometric 2 -d &&
		find $objdir/pack -name "*.pack" | sort >after &&

		# The progression (3, 3, 6) is combined into one new pack.
		test_line_count = 1 after
	)
'

test_expect_success '--geometric with small-pack rollup' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		test_commit_bulk --start=1 1 && # 3 objects
		test_commit_bulk --start=2 1 && # 3 objects
		find $objdir/pack -name "*.pack" | sort >small &&
		test_commit_bulk --start=3 4 && # 12 objects
		test_commit_bulk --start=7 8 && # 24 objects
		find $objdir/pack -name "*.pack" | sort >before &&

		git repack --geometric 2 -d &&

		# Three packs in total; two of the existing large ones, and one
		# new one.
		find $objdir/pack -name "*.pack" | sort >after &&
		test_line_count = 3 after &&
		comm -3 small before | tr -d "\t" >large &&
		grep -qFf large after
	)
'

test_expect_success '--geometric with small- and large-pack rollup' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		# size(small1) + size(small2) > size(medium) / 2
		test_commit_bulk --start=1 1 && # 3 objects
		test_commit_bulk --start=2 1 && # 3 objects
		test_commit_bulk --start=2 3 && # 7 objects
		test_commit_bulk --start=6 9 && # 27 objects &&

		find $objdir/pack -name "*.pack" | sort >before &&

		git repack --geometric 2 -d &&

		find $objdir/pack -name "*.pack" | sort >after &&
		comm -12 before after >untouched &&

		# Two packs in total; the largest pack from before running "git
		# repack", and one new one.
		test_line_count = 1 untouched &&
		test_line_count = 2 after
	)
'

test_expect_success '--geometric ignores kept packs' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		test_commit kept && # 3 objects
		test_commit pack && # 3 objects

		KEPT=$(git pack-objects --revs $objdir/pack/pack <<-EOF
		refs/tags/kept
		EOF
		) &&
		PACK=$(git pack-objects --revs $objdir/pack/pack <<-EOF
		refs/tags/pack
		^refs/tags/kept
		EOF
		) &&

		# neither pack contains more than twice the number of objects in
		# the other, so they should be combined. but, marking one as
		# .kept on disk will "freeze" it, so the pack structure should
		# remain unchanged.
		touch $objdir/pack/pack-$KEPT.keep &&

		find $objdir/pack -name "*.pack" | sort >before &&
		git repack --geometric 2 -d &&
		find $objdir/pack -name "*.pack" | sort >after &&

		# both packs should still exist
		test_path_is_file $objdir/pack/pack-$KEPT.pack &&
		test_path_is_file $objdir/pack/pack-$PACK.pack &&

		# and no new packs should be created
		test_cmp before after &&

		# Passing --pack-kept-objects causes packs with a .keep file to
		# be repacked, too.
		git repack --geometric 2 -d --pack-kept-objects &&

		# After repacking, two packs remain: one new one (containing the
		# objects in both the .keep and non-kept pack), and the .keep
		# pack (since `--pack-kept-objects -d` does not actually delete
		# the kept pack).
		find $objdir/pack -name "*.pack" >after &&
		test_line_count = 2 after
	)
'

test_expect_success '--geometric ignores --keep-pack packs' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		# Create two equal-sized packs
		test_commit kept && # 3 objects
		git repack -d &&
		test_commit pack && # 3 objects
		git repack -d &&

		find $objdir/pack -type f -name "*.pack" | sort >packs.before &&
		git repack --geometric 2 -dm \
			--keep-pack="$(basename "$(head -n 1 packs.before)")" >out &&
		find $objdir/pack -type f -name "*.pack" | sort >packs.after &&

		# Packs should not have changed (only one non-kept pack, no
		# loose objects), but $midx should now exist.
		grep "Nothing new to pack" out &&
		test_path_is_file $midx &&

		test_cmp packs.before packs.after &&

		git fsck
	)
'

test_expect_success '--geometric chooses largest MIDX preferred pack' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		# These packs already form a geometric progression.
		test_commit_bulk --start=1 1 && # 3 objects
		test_commit_bulk --start=2 2 && # 6 objects
		ls $objdir/pack/pack-*.idx >before &&
		test_commit_bulk --start=4 4 && # 12 objects
		ls $objdir/pack/pack-*.idx >after &&

		git repack --geometric 2 -dbm &&

		comm -3 before after | xargs -n 1 basename >expect &&
		test-tool read-midx --preferred-pack $objdir >actual &&

		test_cmp expect actual
	)
'

test_expect_success '--geometric with pack.packSizeLimit' '
	git init pack-rewrite &&
	test_when_finished "rm -fr pack-rewrite" &&
	(
		cd pack-rewrite &&

		test-tool genrandom foo 1048576 >foo &&
		test-tool genrandom bar 1048576 >bar &&

		git add foo bar &&
		test_tick &&
		git commit -m base &&

		git rev-parse HEAD:foo HEAD:bar >p1.objects &&
		git rev-parse HEAD HEAD^{tree} >p2.objects &&

		# These two packs each contain two objects, so the following
		# `--geometric` repack will try to combine them.
		p1="$(git pack-objects $packdir/pack <p1.objects)" &&
		p2="$(git pack-objects $packdir/pack <p2.objects)" &&

		# Remove any loose objects in packs, since we do not want extra
		# copies around (which would mask over potential object
		# corruption issues).
		git prune-packed &&

		# Both p1 and p2 will be rolled up, but pack-objects will write
		# three packs:
		#
		#   - one containing object "foo",
		#   - another containing object "bar",
		#   - a final pack containing the commit and tree objects
		#     (identical to p2 above)
		git repack --geometric 2 -d --max-pack-size=1048576 &&

		# Ensure `repack` can detect that the third pack it wrote
		# (containing just the tree and commit objects) was identical to
		# one that was below the geometric split, so that we can save it
		# from deletion.
		#
		# If `repack` fails to do that, we will incorrectly delete p2,
		# causing object corruption.
		git fsck
	)
'

test_done
