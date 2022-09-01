#!/bin/sh

test_description='cruft pack related pack-objects tests'
. ./test-lib.sh

objdir=.git/objects
packdir=$objdir/pack

basic_cruft_pack_tests () {
	expire="$1"

	test_expect_success "unreachable loose objects are packed (expire $expire)" '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit base &&
			git repack -Ad &&
			test_commit loose &&

			test-tool chmtime +2000 "$objdir/$(test_oid_to_path \
				$(git rev-parse loose:loose.t))" &&
			test-tool chmtime +1000 "$objdir/$(test_oid_to_path \
				$(git rev-parse loose^{tree}))" &&

			(
				git rev-list --objects --no-object-names base..loose |
				while read oid
				do
					path="$objdir/$(test_oid_to_path "$oid")" &&
					printf "%s %d\n" "$oid" "$(test-tool chmtime --get "$path")" ||
					echo "object list generation failed for $oid"
				done |
				sort -k1
			) >expect &&

			keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
			cruft="$(echo $keep | git pack-objects --cruft \
				--cruft-expiration="$expire" $packdir/pack)" &&
			test-tool pack-mtimes "pack-$cruft.mtimes" >actual &&

			test_cmp expect actual
		)
	'

	test_expect_success "unreachable packed objects are packed (expire $expire)" '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit packed &&
			git repack -Ad &&
			test_commit other &&

			git rev-list --objects --no-object-names packed.. >objects &&
			keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
			other="$(git pack-objects --delta-base-offset \
				$packdir/pack <objects)" &&
			git prune-packed &&

			test-tool chmtime --get -100 "$packdir/pack-$other.pack" >expect &&

			cruft="$(git pack-objects --cruft --cruft-expiration="$expire" $packdir/pack <<-EOF
			$keep
			-pack-$other.pack
			EOF
			)" &&
			test-tool pack-mtimes "pack-$cruft.mtimes" >actual.raw &&

			cut -d" " -f2 <actual.raw | sort -u >actual &&

			test_cmp expect actual
		)
	'

	test_expect_success "unreachable cruft objects are repacked (expire $expire)" '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit packed &&
			git repack -Ad &&
			test_commit other &&

			git rev-list --objects --no-object-names packed.. >objects &&
			keep="$(basename "$(ls $packdir/pack-*.pack)")" &&

			cruft_a="$(echo $keep | git pack-objects --cruft --cruft-expiration="$expire" $packdir/pack)" &&
			git prune-packed &&
			cruft_b="$(git pack-objects --cruft --cruft-expiration="$expire" $packdir/pack <<-EOF
			$keep
			-pack-$cruft_a.pack
			EOF
			)" &&

			test-tool pack-mtimes "pack-$cruft_a.mtimes" >expect.raw &&
			test-tool pack-mtimes "pack-$cruft_b.mtimes" >actual.raw &&

			sort <expect.raw >expect &&
			sort <actual.raw >actual &&

			test_cmp expect actual
		)
	'

	test_expect_success "multiple cruft packs (expire $expire)" '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit reachable &&
			git repack -Ad &&
			keep="$(basename "$(ls $packdir/pack-*.pack)")" &&

			test_commit cruft &&
			loose="$objdir/$(test_oid_to_path $(git rev-parse cruft))" &&

			# generate three copies of the cruft object in different
			# cruft packs, each with a unique mtime:
			#   - one expired (1000 seconds ago)
			#   - two non-expired (one 1000 seconds in the future,
			#     one 1500 seconds in the future)
			test-tool chmtime =-1000 "$loose" &&
			git pack-objects --cruft $packdir/pack-A <<-EOF &&
			$keep
			EOF
			test-tool chmtime =+1000 "$loose" &&
			git pack-objects --cruft $packdir/pack-B <<-EOF &&
			$keep
			-$(basename $(ls $packdir/pack-A-*.pack))
			EOF
			test-tool chmtime =+1500 "$loose" &&
			git pack-objects --cruft $packdir/pack-C <<-EOF &&
			$keep
			-$(basename $(ls $packdir/pack-A-*.pack))
			-$(basename $(ls $packdir/pack-B-*.pack))
			EOF

			# ensure the resulting cruft pack takes the most recent
			# mtime among all copies
			cruft="$(git pack-objects --cruft \
				--cruft-expiration="$expire" \
				$packdir/pack <<-EOF
			$keep
			-$(basename $(ls $packdir/pack-A-*.pack))
			-$(basename $(ls $packdir/pack-B-*.pack))
			-$(basename $(ls $packdir/pack-C-*.pack))
			EOF
			)" &&

			test-tool pack-mtimes "$(basename $(ls $packdir/pack-C-*.mtimes))" >expect.raw &&
			test-tool pack-mtimes "pack-$cruft.mtimes" >actual.raw &&

			sort expect.raw >expect &&
			sort actual.raw >actual &&
			test_cmp expect actual
		)
	'

	test_expect_success "cruft packs tolerate missing trees (expire $expire)" '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit reachable &&
			test_commit cruft &&

			tree="$(git rev-parse cruft^{tree})" &&

			git reset --hard reachable &&
			git tag -d cruft &&
			git reflog expire --all --expire=all &&

			# remove the unreachable tree, but leave the commit
			# which has it as its root tree intact
			rm -fr "$objdir/$(test_oid_to_path "$tree")" &&

			git repack -Ad &&
			basename $(ls $packdir/pack-*.pack) >in &&
			git pack-objects --cruft --cruft-expiration="$expire" \
				$packdir/pack <in
		)
	'

	test_expect_success "cruft packs tolerate missing blobs (expire $expire)" '
		git init repo &&
		test_when_finished "rm -fr repo" &&
		(
			cd repo &&

			test_commit reachable &&
			test_commit cruft &&

			blob="$(git rev-parse cruft:cruft.t)" &&

			git reset --hard reachable &&
			git tag -d cruft &&
			git reflog expire --all --expire=all &&

			# remove the unreachable blob, but leave the commit (and
			# the root tree of that commit) intact
			rm -fr "$objdir/$(test_oid_to_path "$blob")" &&

			git repack -Ad &&
			basename $(ls $packdir/pack-*.pack) >in &&
			git pack-objects --cruft --cruft-expiration="$expire" \
				$packdir/pack <in
		)
	'
}

basic_cruft_pack_tests never
basic_cruft_pack_tests 2.weeks.ago

test_expect_success 'cruft tags rescue tagged objects' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit packed &&
		git repack -Ad &&

		test_commit tagged &&
		git tag -a annotated -m tag &&

		git rev-list --objects --no-object-names packed.. >objects &&
		while read oid
		do
			test-tool chmtime -1000 \
				"$objdir/$(test_oid_to_path $oid)" || exit 1
		done <objects &&

		test-tool chmtime -500 \
			"$objdir/$(test_oid_to_path $(git rev-parse annotated))" &&

		keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
		cruft="$(echo $keep | git pack-objects --cruft \
			--cruft-expiration=750.seconds.ago \
			$packdir/pack)" &&
		test-tool pack-mtimes "pack-$cruft.mtimes" >actual.raw &&
		cut -f1 -d" " <actual.raw | sort >actual &&

		(
			cat objects &&
			git rev-parse annotated
		) >expect.raw &&
		sort <expect.raw >expect &&

		test_cmp expect actual &&
		cat actual
	)
'

test_expect_success 'cruft commits rescue parents, trees' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit packed &&
		git repack -Ad &&

		test_commit old &&
		test_commit new &&

		git rev-list --objects --no-object-names packed..new >objects &&
		while read object
		do
			test-tool chmtime -1000 \
				"$objdir/$(test_oid_to_path $object)" || exit 1
		done <objects &&
		test-tool chmtime +500 "$objdir/$(test_oid_to_path \
			$(git rev-parse HEAD))" &&

		keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
		cruft="$(echo $keep | git pack-objects --cruft \
			--cruft-expiration=750.seconds.ago \
			$packdir/pack)" &&
		test-tool pack-mtimes "pack-$cruft.mtimes" >actual.raw &&

		cut -d" " -f1 <actual.raw | sort >actual &&
		sort <objects >expect &&

		test_cmp expect actual
	)
'

test_expect_success 'cruft trees rescue sub-trees, blobs' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit packed &&
		git repack -Ad &&

		mkdir -p dir/sub &&
		echo foo >foo &&
		echo bar >dir/bar &&
		echo baz >dir/sub/baz &&

		test_tick &&
		git add . &&
		git commit -m "pruned" &&

		test-tool chmtime -1000 "$objdir/$(test_oid_to_path $(git rev-parse HEAD))" &&
		test-tool chmtime -1000 "$objdir/$(test_oid_to_path $(git rev-parse HEAD^{tree}))" &&
		test-tool chmtime -1000 "$objdir/$(test_oid_to_path $(git rev-parse HEAD:foo))" &&
		test-tool chmtime  -500 "$objdir/$(test_oid_to_path $(git rev-parse HEAD:dir))" &&
		test-tool chmtime -1000 "$objdir/$(test_oid_to_path $(git rev-parse HEAD:dir/bar))" &&
		test-tool chmtime -1000 "$objdir/$(test_oid_to_path $(git rev-parse HEAD:dir/sub))" &&
		test-tool chmtime -1000 "$objdir/$(test_oid_to_path $(git rev-parse HEAD:dir/sub/baz))" &&

		keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
		cruft="$(echo $keep | git pack-objects --cruft \
			--cruft-expiration=750.seconds.ago \
			$packdir/pack)" &&
		test-tool pack-mtimes "pack-$cruft.mtimes" >actual.raw &&
		cut -f1 -d" " <actual.raw | sort >actual &&

		git rev-parse HEAD:dir HEAD:dir/bar HEAD:dir/sub HEAD:dir/sub/baz >expect.raw &&
		sort <expect.raw >expect &&

		test_cmp expect actual
	)
'

test_expect_success 'expired objects are pruned' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit packed &&
		git repack -Ad &&

		test_commit pruned &&

		git rev-list --objects --no-object-names packed..pruned >objects &&
		while read object
		do
			test-tool chmtime -1000 \
				"$objdir/$(test_oid_to_path $object)" || exit 1
		done <objects &&

		keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
		cruft="$(echo $keep | git pack-objects --cruft \
			--cruft-expiration=750.seconds.ago \
			$packdir/pack)" &&

		test-tool pack-mtimes "pack-$cruft.mtimes" >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'repack --cruft generates a cruft pack' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit reachable &&
		git branch -M main &&
		git checkout --orphan other &&
		test_commit unreachable &&

		git checkout main &&
		git branch -D other &&
		git tag -d unreachable &&
		# objects are not cruft if they are contained in the reflogs
		git reflog expire --all --expire=all &&

		git rev-list --objects --all --no-object-names >reachable.raw &&
		git cat-file --batch-all-objects --batch-check="%(objectname)" >objects &&
		sort <reachable.raw >reachable &&
		comm -13 reachable objects >unreachable &&

		git repack --cruft -d &&

		cruft=$(basename $(ls $packdir/pack-*.mtimes) .mtimes) &&
		pack=$(basename $(ls $packdir/pack-*.pack | grep -v $cruft) .pack) &&

		git show-index <$packdir/$pack.idx >actual.raw &&
		cut -f2 -d" " actual.raw | sort >actual &&
		test_cmp reachable actual &&

		git show-index <$packdir/$cruft.idx >actual.raw &&
		cut -f2 -d" " actual.raw | sort >actual &&
		test_cmp unreachable actual
	)
'

test_expect_success 'loose objects mtimes upsert others' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit reachable &&
		git repack -Ad &&
		git branch -M main &&

		git checkout --orphan other &&
		test_commit cruft &&
		# incremental repack, leaving existing objects loose (so
		# they can be "freshened")
		git repack &&

		tip="$(git rev-parse cruft)" &&
		path="$objdir/$(test_oid_to_path "$tip")" &&
		test-tool chmtime --get +1000 "$path" >expect &&

		git checkout main &&
		git branch -D other &&
		git tag -d cruft &&
		git reflog expire --all --expire=all &&

		git repack --cruft -d &&

		mtimes="$(basename $(ls $packdir/pack-*.mtimes))" &&
		test-tool pack-mtimes "$mtimes" >actual.raw &&
		grep "$tip" actual.raw | cut -d" " -f2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'expiring cruft objects with git gc' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit reachable &&
		git branch -M main &&
		git checkout --orphan other &&
		test_commit unreachable &&

		git checkout main &&
		git branch -D other &&
		git tag -d unreachable &&
		# objects are not cruft if they are contained in the reflogs
		git reflog expire --all --expire=all &&

		git rev-list --objects --all --no-object-names >reachable.raw &&
		git cat-file --batch-all-objects --batch-check="%(objectname)" >objects &&
		sort <reachable.raw >reachable &&
		comm -13 reachable objects >unreachable &&

		# Write a cruft pack containing all unreachable objects.
		git gc --cruft --prune="01-01-1980" &&

		mtimes=$(ls .git/objects/pack/pack-*.mtimes) &&
		test_path_is_file $mtimes &&

		# Prune all unreachable objects from the cruft pack.
		git gc --cruft --prune=now &&

		git cat-file --batch-all-objects --batch-check="%(objectname)" >objects &&

		comm -23 unreachable objects >removed &&
		test_cmp unreachable removed &&
		test_path_is_missing $mtimes
	)
'

test_expect_success 'cruft packs are not included in geometric repack' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit reachable &&
		git repack -Ad &&
		git branch -M main &&

		git checkout --orphan other &&
		test_commit cruft &&
		git repack -d &&

		git checkout main &&
		git branch -D other &&
		git tag -d cruft &&
		git reflog expire --all --expire=all &&

		git repack --cruft &&

		find $packdir -type f | sort >before &&
		git repack --geometric=2 -d &&
		find $packdir -type f | sort >after &&

		test_cmp before after
	)
'

test_expect_success 'repack --geometric collects once-cruft objects' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit reachable &&
		git repack -Ad &&
		git branch -M main &&

		git checkout --orphan other &&
		git rm -rf . &&
		test_commit --no-tag cruft &&
		cruft="$(git rev-parse HEAD)" &&

		git checkout main &&
		git branch -D other &&
		git reflog expire --all --expire=all &&

		# Pack the objects created in the previous step into a cruft
		# pack. Intentionally leave loose copies of those objects
		# around so we can pick them up in a subsequent --geometric
		# reapack.
		git repack --cruft &&

		# Now make those objects reachable, and ensure that they are
		# packed into the new pack created via a --geometric repack.
		git update-ref refs/heads/other $cruft &&

		# Without this object, the set of unpacked objects is exactly
		# the set of objects already in the cruft pack. Tweak that set
		# to ensure we do not overwrite the cruft pack entirely.
		test_commit reachable2 &&

		find $packdir -name "pack-*.idx" | sort >before &&
		git repack --geometric=2 -d &&
		find $packdir -name "pack-*.idx" | sort >after &&

		{
			git rev-list --objects --no-object-names $cruft &&
			git rev-list --objects --no-object-names reachable..reachable2
		} >want.raw &&
		sort want.raw >want &&

		pack=$(comm -13 before after) &&
		git show-index <$pack >objects.raw &&

		cut -d" " -f2 objects.raw | sort >got &&

		test_cmp want got
	)
'

test_expect_success 'cruft repack with no reachable objects' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit base &&
		git repack -ad &&

		base="$(git rev-parse base)" &&

		git for-each-ref --format="delete %(refname)" >in &&
		git update-ref --stdin <in &&
		git reflog expire --all --expire=all &&
		rm -fr .git/index &&

		git repack --cruft -d &&

		git cat-file -t $base
	)
'

test_expect_success 'cruft repack ignores --max-pack-size' '
	git init max-pack-size &&
	(
		cd max-pack-size &&
		test_commit base &&
		# two cruft objects which exceed the maximum pack size
		test-tool genrandom foo 1048576 | git hash-object --stdin -w &&
		test-tool genrandom bar 1048576 | git hash-object --stdin -w &&
		git repack --cruft --max-pack-size=1M &&
		find $packdir -name "*.mtimes" >cruft &&
		test_line_count = 1 cruft &&
		test-tool pack-mtimes "$(basename "$(cat cruft)")" >objects &&
		test_line_count = 2 objects
	)
'

test_expect_success 'cruft repack ignores pack.packSizeLimit' '
	(
		cd max-pack-size &&
		# repack everything back together to remove the existing cruft
		# pack (but to keep its objects)
		git repack -adk &&
		git -c pack.packSizeLimit=1M repack --cruft &&
		# ensure the same post condition is met when --max-pack-size
		# would otherwise be inferred from the configuration
		find $packdir -name "*.mtimes" >cruft &&
		test_line_count = 1 cruft &&
		test-tool pack-mtimes "$(basename "$(cat cruft)")" >objects &&
		test_line_count = 2 objects
	)
'

test_expect_success 'cruft repack respects repack.cruftWindow' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit base &&

		GIT_TRACE2_EVENT=$(pwd)/event.trace \
		git -c pack.window=1 -c repack.cruftWindow=2 repack \
		       --cruft --window=3 &&

		grep "pack-objects.*--window=2.*--cruft" event.trace
	)
'

test_expect_success 'cruft repack respects --window by default' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit base &&

		GIT_TRACE2_EVENT=$(pwd)/event.trace \
		git -c pack.window=2 repack --cruft --window=3 &&

		grep "pack-objects.*--window=3.*--cruft" event.trace
	)
'

test_expect_success 'cruft repack respects --quiet' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit base &&
		GIT_PROGRESS_DELAY=0 git repack --cruft --quiet 2>err &&
		test_must_be_empty err
	)
'

test_expect_success 'cruft --local drops unreachable objects' '
	git init alternate &&
	git init repo &&
	test_when_finished "rm -fr alternate repo" &&

	test_commit -C alternate base &&
	# Pack all objects in alterate so that the cruft repack in "repo" sees
	# the object it dropped due to `--local` as packed. Otherwise this
	# object would not appear packed anywhere (since it is not packed in
	# alternate and likewise not part of the cruft pack in the other repo
	# because of `--local`).
	git -C alternate repack -ad &&

	(
		cd repo &&

		object="$(git -C ../alternate rev-parse HEAD:base.t)" &&
		git -C ../alternate cat-file -p $object >contents &&

		# Write some reachable objects and two unreachable ones: one
		# that the alternate has and another that is unique.
		test_commit other &&
		git hash-object -w -t blob contents &&
		cruft="$(echo cruft | git hash-object -w -t blob --stdin)" &&

		( cd ../alternate/.git/objects && pwd ) \
		       >.git/objects/info/alternates &&

		test_path_is_file $objdir/$(test_oid_to_path $cruft) &&
		test_path_is_file $objdir/$(test_oid_to_path $object) &&

		git repack -d --cruft --local &&

		test-tool pack-mtimes "$(basename $(ls $packdir/pack-*.mtimes))" \
		       >objects &&
		! grep $object objects &&
		grep $cruft objects
	)
'

test_expect_success 'MIDX bitmaps tolerate reachable cruft objects' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit reachable &&
		test_commit cruft &&
		unreachable="$(git rev-parse cruft)" &&

		git reset --hard $unreachable^ &&
		git tag -d cruft &&
		git reflog expire --all --expire=all &&

		git repack --cruft -d &&

		# resurrect the unreachable object via a new commit. the
		# new commit will get selected for a bitmap, but be
		# missing one of its parents from the selected packs.
		git reset --hard $unreachable &&
		test_commit resurrect &&

		git repack --write-midx --write-bitmap-index --geometric=2 -d
	)
'

test_expect_success 'cruft objects are freshend via loose' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		echo "cruft" >contents &&
		blob="$(git hash-object -w -t blob contents)" &&
		loose="$objdir/$(test_oid_to_path $blob)" &&

		test_commit base &&

		git repack --cruft -d &&

		test_path_is_missing "$loose" &&
		test-tool pack-mtimes "$(basename "$(ls $packdir/pack-*.mtimes)")" >cruft &&
		grep "$blob" cruft &&

		# write the same object again
		git hash-object -w -t blob contents &&

		test_path_is_file "$loose"
	)
'

test_done
