#!/bin/sh

test_description='handling of missing objects in rev-list'

. ./test-lib.sh

# We setup the repository with two commits, this way HEAD is always
# available and we can hide commit 1.
test_expect_success 'create repository and alternate directory' '
	test_commit 1 &&
	test_commit 2 &&
	test_commit 3 &&
	git tag -m "tag message" annot_tag HEAD~1 &&
	git tag regul_tag HEAD~1 &&
	git branch a_branch HEAD~1
'

# We manually corrupt the repository, which means that the commit-graph may
# contain references to already-deleted objects. We thus need to enable
# commit-graph paranoia to not returned these deleted commits from the graph.
GIT_COMMIT_GRAPH_PARANOIA=true
export GIT_COMMIT_GRAPH_PARANOIA

for obj in "HEAD~1" "HEAD~1^{tree}" "HEAD:1.t"
do
	test_expect_success "rev-list --missing=error fails with missing object $obj" '
		oid="$(git rev-parse $obj)" &&
		path=".git/objects/$(test_oid_to_path $oid)" &&

		mv "$path" "$path.hidden" &&
		test_when_finished "mv $path.hidden $path" &&

		test_must_fail git rev-list --missing=error --objects \
			--no-object-names HEAD
	'
done

for obj in "HEAD~1" "HEAD~1^{tree}" "HEAD:1.t"
do
	for action in "allow-any" "print"
	do
		test_expect_success "rev-list --missing=$action with missing $obj" '
			oid="$(git rev-parse $obj)" &&
			path=".git/objects/$(test_oid_to_path $oid)" &&

			# Before the object is made missing, we use rev-list to
			# get the expected oids.
			git rev-list --objects --no-object-names \
				HEAD ^$obj >expect.raw &&

			# Blobs are shared by all commits, so even though a commit/tree
			# might be skipped, its blob must be accounted for.
			if test $obj != "HEAD:1.t"
			then
				echo $(git rev-parse HEAD:1.t) >>expect.raw &&
				echo $(git rev-parse HEAD:2.t) >>expect.raw
			fi &&

			mv "$path" "$path.hidden" &&
			test_when_finished "mv $path.hidden $path" &&

			git rev-list --missing=$action --objects --no-object-names \
				HEAD >actual.raw &&

			# When the action is to print, we should also add the missing
			# oid to the expect list.
			case $action in
			allow-any)
				;;
			print)
				grep ?$oid actual.raw &&
				echo ?$oid >>expect.raw
				;;
			esac &&

			sort actual.raw >actual &&
			sort expect.raw >expect &&
			test_cmp expect actual
		'
	done
done

for missing_tip in "annot_tag" "regul_tag" "a_branch" "HEAD~1" "HEAD~1^{tree}" "HEAD:1.t"
do
	# We want to check that things work when both
	#   - all the tips passed are missing (case existing_tip = ""), and
	#   - there is one missing tip and one existing tip (case existing_tip = "HEAD")
	for existing_tip in "" "HEAD"
	do
		for action in "allow-any" "print"
		do
			test_expect_success "--missing=$action with tip '$missing_tip' missing and tip '$existing_tip'" '
				# Before the object is made missing, we use rev-list to
				# get the expected oids.
				if test "$existing_tip" = "HEAD"
				then
					git rev-list --objects --no-object-names \
						HEAD ^$missing_tip >expect.raw
				else
					>expect.raw
				fi &&

				# Blobs are shared by all commits, so even though a commit/tree
				# might be skipped, its blob must be accounted for.
				if test "$existing_tip" = "HEAD" && test $missing_tip != "HEAD:1.t"
				then
					echo $(git rev-parse HEAD:1.t) >>expect.raw &&
					echo $(git rev-parse HEAD:2.t) >>expect.raw
				fi &&

				missing_oid="$(git rev-parse $missing_tip)" &&

				if test "$missing_tip" = "annot_tag"
				then
					oid="$(git rev-parse $missing_tip^{commit})" &&
					echo "$missing_oid" >>expect.raw
				else
					oid="$missing_oid"
				fi &&

				path=".git/objects/$(test_oid_to_path $oid)" &&

				mv "$path" "$path.hidden" &&
				test_when_finished "mv $path.hidden $path" &&

				git rev-list --missing=$action --objects --no-object-names \
				     $missing_oid $existing_tip >actual.raw &&

				# When the action is to print, we should also add the missing
				# oid to the expect list.
				case $action in
				allow-any)
					;;
				print)
					grep ?$oid actual.raw &&
					echo ?$oid >>expect.raw
					;;
				esac &&

				sort actual.raw >actual &&
				sort expect.raw >expect &&
				test_cmp expect actual
			'
		done
	done
done

for obj in "HEAD~1" "HEAD^{tree}" "HEAD:foo" "HEAD:foo/bar" "HEAD:baz baz"
do
	test_expect_success "--missing=print-info with missing '$obj'" '
		test_when_finished rm -rf missing-info &&

		git init missing-info &&
		(
			cd missing-info &&
			git commit --allow-empty -m first &&

			mkdir foo &&
			echo bar >foo/bar &&
			echo baz >"baz baz" &&
			echo bat >bat\" &&
			git add -A &&
			git commit -m second &&

			oid="$(git rev-parse "$obj")" &&
			path=".git/objects/$(test_oid_to_path $oid)" &&
			type_info=" type=$(git cat-file -t $oid)" &&

			case $obj in
			HEAD:foo)
				path_info=" path=foo"
				;;
			HEAD:foo/bar)
				path_info=" path=foo/bar"
				;;
			"HEAD:baz baz")
				path_info=" path=\"baz baz\""
				;;
			"HEAD:bat\"")
				path_info=" path=\"bat\\\"\""
				;;
			esac &&

			# Before the object is made missing, we use rev-list to
			# get the expected oids.
			git rev-list --objects --no-object-names \
				HEAD ^"$obj" >expect.raw &&
			echo "?$oid$path_info$type_info" >>expect.raw &&

			mv "$path" "$path.hidden" &&
			git rev-list --objects --no-object-names \
				--missing=print-info HEAD >actual.raw &&

			sort actual.raw >actual &&
			sort expect.raw >expect &&
			test_cmp expect actual
		)
	'
done

test_expect_success "-z nul-delimited --missing" '
	test_when_finished rm -rf repo &&

	git init repo &&
	(
		cd repo &&
		git commit --allow-empty -m first &&

		path="foo bar" &&
		echo foobar >"$path" &&
		git add -A &&
		git commit -m second &&

		oid=$(git rev-parse "HEAD:$path") &&
		type="$(git cat-file -t $oid)" &&

		obj_path=".git/objects/$(test_oid_to_path $oid)" &&

		git rev-list -z --objects --no-object-names \
			HEAD ^"$oid" >expect &&
		printf "%s\0missing=yes\0path=%s\0type=%s\0" "$oid" "$path" \
			"$type" >>expect &&

		mv "$obj_path" "$obj_path.hidden" &&
		git rev-list -z --objects --no-object-names \
			--missing=print-info HEAD >actual &&

		test_cmp expect actual
	)
'

test_done
