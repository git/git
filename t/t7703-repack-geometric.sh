#!/bin/sh

test_description='git repack --geometric works correctly'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

GIT_TEST_MULTI_PACK_INDEX=0

objdir=.git/objects
packdir=$objdir/pack
midx=$objdir/pack/multi-pack-index

packed_objects () {
	git show-index <"$1" >tmp-object-list &&
	cut -d' ' -f2 tmp-object-list | sort &&
	rm tmp-object-list
 }

test_expect_success '--geometric with no packs' '
	git init geometric &&
	test_when_finished "rm -fr geometric" &&
	(
		cd geometric &&

		git repack --write-midx --geometric 2 >out &&
		test_grep "Nothing new to pack" out
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

		test_grep "Nothing new to pack" out
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

test_expect_success '--geometric --write-midx with packfiles in main and alternate ODB' '
	test_when_finished "rm -fr shared member" &&

	# Create a shared repository that will serve as the alternate object
	# database for the member linked to it. It has got some objects on its
	# own that are packed into a single packfile.
	git init shared &&
	test_commit -C shared common-object &&
	git -C shared repack -ad &&

	# We create member so that its alternates file points to the shared
	# repository. We then create a commit in it so that git-repack(1) has
	# something to repack.
	# of the shared object database.
	git clone --shared shared member &&
	test_commit -C member unique-object &&
	git -C member repack --geometric=2 --write-midx 2>err &&
	test_must_be_empty err &&

	# We should see that a new packfile was generated.
	find shared/.git/objects/pack -type f -name "*.pack" >packs &&
	test_line_count = 1 packs &&

	# We should also see a multi-pack-index. This multi-pack-index should
	# never refer to any packfiles in the alternate object database.
	test_path_is_file member/.git/objects/pack/multi-pack-index &&
	test-tool read-midx member/.git/objects >packs.midx &&
	grep "^pack-.*\.idx$" packs.midx | sort >actual &&
	basename member/.git/objects/pack/pack-*.idx >expect &&
	test_cmp expect actual
'

test_expect_success '--geometric --with-midx with no local objects' '
	test_when_finished "rm -fr shared member" &&

	# Create a repository with a single packfile that acts as alternate
	# object database.
	git init shared &&
	test_commit -C shared "shared-objects" &&
	git -C shared repack -ad &&

	# Create a second repository linked to the first one and perform a
	# geometric repack on it.
	git clone --shared shared member &&
	git -C member repack --geometric 2 --write-midx 2>err &&
	test_must_be_empty err &&

	# Assert that we wrote neither a new packfile nor a multi-pack-index.
	# We should not have a packfile because the single packfile in the
	# alternate object database does not invalidate the geometric sequence.
	# And we should not have a multi-pack-index because these only index
	# local packfiles, and there are none.
	test_dir_is_empty member/$packdir
'

test_expect_success '--geometric with same pack in main and alternate ODB' '
	test_when_finished "rm -fr shared member" &&

	# Create a repository with a single packfile that acts as alternate
	# object database.
	git init shared &&
	test_commit -C shared "shared-objects" &&
	git -C shared repack -ad &&

	# We create the member repository as an exact copy so that it has the
	# same packfile.
	cp -r shared member &&
	test-tool path-utils real_path shared/.git/objects >member/.git/objects/info/alternates &&
	find shared/.git/objects -type f >expected-files &&

	# Verify that we can repack objects as expected without observing any
	# error. Having the same packfile in both ODBs used to cause an error
	# in git-pack-objects(1).
	git -C member repack --geometric 2 2>err &&
	test_must_be_empty err &&
	# Nothing should have changed.
	find shared/.git/objects -type f >actual-files &&
	test_cmp expected-files actual-files
'

test_expect_success '--geometric -l with non-intact geometric sequence across ODBs' '
	test_when_finished "rm -fr shared member" &&

	git init shared &&
	test_commit_bulk -C shared --start=1 1 &&

	git clone --shared shared member &&
	test_commit_bulk -C member --start=2 1 &&

	# Verify that our assumptions actually hold: both generated packfiles
	# should have three objects and should be non-equal.
	packed_objects shared/.git/objects/pack/pack-*.idx >shared-objects &&
	packed_objects member/.git/objects/pack/pack-*.idx >member-objects &&
	test_line_count = 3 shared-objects &&
	test_line_count = 3 member-objects &&
	! test_cmp shared-objects member-objects &&

	# Perform the geometric repack. With `-l`, we should only see the local
	# packfile and thus arrive at the conclusion that the geometric
	# sequence is intact. We thus expect no changes.
	#
	# Note that we are tweaking mtimes of the packfiles so that we can
	# verify they did not change. This is done in order to detect the case
	# where we do repack objects, but the resulting packfile is the same.
	test-tool chmtime --verbose =0 member/.git/objects/pack/* >expected-member-packs &&
	git -C member repack --geometric=2 -l -d &&
	test-tool chmtime --verbose member/.git/objects/pack/* >actual-member-packs &&
	test_cmp expected-member-packs actual-member-packs &&

	{
		packed_objects shared/.git/objects/pack/pack-*.idx &&
		packed_objects member/.git/objects/pack/pack-*.idx
	} | sort >expected-objects &&

	# On the other hand, when doing a non-local geometric repack we should
	# see both packfiles and thus repack them. We expect that the shared
	# object database was not changed.
	test-tool chmtime --verbose =0 shared/.git/objects/pack/* >expected-shared-packs &&
	git -C member repack --geometric=2 -d &&
	test-tool chmtime --verbose shared/.git/objects/pack/* >actual-shared-packs &&
	test_cmp expected-shared-packs actual-shared-packs &&

	# Furthermore, we expect that the member repository now has a single
	# packfile that contains the combined shared and non-shared objects.
	ls member/.git/objects/pack/pack-*.idx >actual &&
	test_line_count = 1 actual &&
	packed_objects member/.git/objects/pack/pack-*.idx >actual-objects &&
	test_line_count = 6 actual-objects &&
	test_cmp expected-objects actual-objects
'

test_expect_success '--geometric -l disables writing bitmaps with non-local packfiles' '
	test_when_finished "rm -fr shared member" &&

	git init shared &&
	test_commit_bulk -C shared --start=1 1 &&

	git clone --shared shared member &&
	test_commit_bulk -C member --start=2 1 &&

	# When performing a geometric repack with `-l` while connected to an
	# alternate object database that has a packfile we do not have full
	# coverage of objects. As a result, we expect that writing the bitmap
	# will be disabled.
	git -C member repack -l --geometric=2 --write-midx --write-bitmap-index 2>err &&
	cat >expect <<-EOF &&
	warning: disabling bitmap writing, as some objects are not being packed
	EOF
	test_cmp expect err &&
	test_path_is_missing member/.git/objects/pack/multi-pack-index-*.bitmap &&

	# On the other hand, when we repack without `-l`, we should see that
	# the bitmap gets created.
	git -C member repack --geometric=2 --write-midx --write-bitmap-index 2>err &&
	test_must_be_empty err &&
	test_path_is_file member/.git/objects/pack/multi-pack-index-*.bitmap
'

test_done
