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
					printf "%s %d\n" "$oid" "$(test-tool chmtime --get "$path")"
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
				"$objdir/$(test_oid_to_path $oid)"
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
				"$objdir/$(test_oid_to_path $object)"
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
				"$objdir/$(test_oid_to_path $object)"
		done <objects &&

		keep="$(basename "$(ls $packdir/pack-*.pack)")" &&
		cruft="$(echo $keep | git pack-objects --cruft \
			--cruft-expiration=750.seconds.ago \
			$packdir/pack)" &&

		test-tool pack-mtimes "pack-$cruft.mtimes" >actual &&
		test_must_be_empty actual
	)
'

test_done
