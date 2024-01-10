#!/bin/sh

test_description='pack-objects --stdin'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

packed_objects () {
	git show-index <"$1" >tmp-object-list &&
	cut -d' ' -f2 tmp-object-list | sort &&
	rm tmp-object-list
 }

test_expect_success 'setup for --stdin-packs tests' '
	git init stdin-packs &&
	(
		cd stdin-packs &&

		test_commit A &&
		test_commit B &&
		test_commit C &&

		for id in A B C
		do
			git pack-objects .git/objects/pack/pack-$id \
				--incremental --revs <<-EOF || exit 1
			refs/tags/$id
			EOF
		done &&

		ls -la .git/objects/pack
	)
'

test_expect_success '--stdin-packs with excluded packs' '
	(
		cd stdin-packs &&

		PACK_A="$(basename .git/objects/pack/pack-A-*.pack)" &&
		PACK_B="$(basename .git/objects/pack/pack-B-*.pack)" &&
		PACK_C="$(basename .git/objects/pack/pack-C-*.pack)" &&

		git pack-objects test --stdin-packs <<-EOF &&
		$PACK_A
		^$PACK_B
		$PACK_C
		EOF

		(
			git show-index <$(ls .git/objects/pack/pack-A-*.idx) &&
			git show-index <$(ls .git/objects/pack/pack-C-*.idx)
		) >expect.raw &&
		git show-index <$(ls test-*.idx) >actual.raw &&

		cut -d" " -f2 <expect.raw | sort >expect &&
		cut -d" " -f2 <actual.raw | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--stdin-packs is incompatible with --filter' '
	(
		cd stdin-packs &&
		test_must_fail git pack-objects --stdin-packs --stdout \
			--filter=blob:none </dev/null 2>err &&
		test_grep "cannot use --filter with --stdin-packs" err
	)
'

test_expect_success '--stdin-packs is incompatible with --revs' '
	(
		cd stdin-packs &&
		test_must_fail git pack-objects --stdin-packs --revs out \
			</dev/null 2>err &&
		test_grep "cannot use internal rev list with --stdin-packs" err
	)
'

test_expect_success '--stdin-packs with loose objects' '
	(
		cd stdin-packs &&

		PACK_A="$(basename .git/objects/pack/pack-A-*.pack)" &&
		PACK_B="$(basename .git/objects/pack/pack-B-*.pack)" &&
		PACK_C="$(basename .git/objects/pack/pack-C-*.pack)" &&

		test_commit D && # loose

		git pack-objects test2 --stdin-packs --unpacked <<-EOF &&
		$PACK_A
		^$PACK_B
		$PACK_C
		EOF

		(
			git show-index <$(ls .git/objects/pack/pack-A-*.idx) &&
			git show-index <$(ls .git/objects/pack/pack-C-*.idx) &&
			git rev-list --objects --no-object-names \
				refs/tags/C..refs/tags/D

		) >expect.raw &&
		ls -la . &&
		git show-index <$(ls test2-*.idx) >actual.raw &&

		cut -d" " -f2 <expect.raw | sort >expect &&
		cut -d" " -f2 <actual.raw | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--stdin-packs with broken links' '
	(
		cd stdin-packs &&

		# make an unreachable object with a bogus parent
		git cat-file -p HEAD >commit &&
		sed "s/$(git rev-parse HEAD^)/$(test_oid zero)/" <commit |
		git hash-object -w -t commit --stdin >in &&

		git pack-objects .git/objects/pack/pack-D <in &&

		PACK_A="$(basename .git/objects/pack/pack-A-*.pack)" &&
		PACK_B="$(basename .git/objects/pack/pack-B-*.pack)" &&
		PACK_C="$(basename .git/objects/pack/pack-C-*.pack)" &&
		PACK_D="$(basename .git/objects/pack/pack-D-*.pack)" &&

		git pack-objects test3 --stdin-packs --unpacked <<-EOF &&
		$PACK_A
		^$PACK_B
		$PACK_C
		$PACK_D
		EOF

		(
			git show-index <$(ls .git/objects/pack/pack-A-*.idx) &&
			git show-index <$(ls .git/objects/pack/pack-C-*.idx) &&
			git show-index <$(ls .git/objects/pack/pack-D-*.idx) &&
			git rev-list --objects --no-object-names \
				refs/tags/C..refs/tags/D
		) >expect.raw &&
		git show-index <$(ls test3-*.idx) >actual.raw &&

		cut -d" " -f2 <expect.raw | sort >expect &&
		cut -d" " -f2 <actual.raw | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'pack-objects --stdin with duplicate packfile' '
	test_when_finished "rm -fr repo" &&

	git init repo &&
	(
		cd repo &&
		test_commit "commit" &&
		git repack -ad &&

		{
			basename .git/objects/pack/pack-*.pack &&
			basename .git/objects/pack/pack-*.pack
		} >packfiles &&

		git pack-objects --stdin-packs generated-pack <packfiles &&
		packed_objects .git/objects/pack/pack-*.idx >expect &&
		packed_objects generated-pack-*.idx >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'pack-objects --stdin with same packfile excluded and included' '
	test_when_finished "rm -fr repo" &&

	git init repo &&
	(
		cd repo &&
		test_commit "commit" &&
		git repack -ad &&

		{
			basename .git/objects/pack/pack-*.pack &&
			printf "^%s\n" "$(basename .git/objects/pack/pack-*.pack)"
		} >packfiles &&

		git pack-objects --stdin-packs generated-pack <packfiles &&
		packed_objects generated-pack-*.idx >packed-objects &&
		test_must_be_empty packed-objects
	)
'

test_expect_success 'pack-objects --stdin with packfiles from alternate object database' '
	test_when_finished "rm -fr shared member" &&

	# Set up a shared repository with a single packfile.
	git init shared &&
	test_commit -C shared "shared-objects" &&
	git -C shared repack -ad &&
	basename shared/.git/objects/pack/pack-*.pack >packfile &&

	# Set up a repository that is connected to the shared repository. This
	# repository has no objects on its own, but we still expect to be able
	# to pack objects from its alternate.
	git clone --shared shared member &&
	git -C member pack-objects --stdin-packs generated-pack <packfile &&
	test_cmp shared/.git/objects/pack/pack-*.pack member/generated-pack-*.pack
'

test_expect_success 'pack-objects --stdin with packfiles from main and alternate object database' '
	test_when_finished "rm -fr shared member" &&

	# Set up a shared repository with a single packfile.
	git init shared &&
	test_commit -C shared "shared-commit" &&
	git -C shared repack -ad &&

	# Set up a repository that is connected to the shared repository. This
	# repository has a second packfile so that we can verify that it is
	# possible to write packs that include packfiles from different object
	# databases.
	git clone --shared shared member &&
	test_commit -C member "local-commit" &&
	git -C member repack -dl &&

	{
		basename shared/.git/objects/pack/pack-*.pack &&
		basename member/.git/objects/pack/pack-*.pack
	} >packfiles &&

	{
		packed_objects shared/.git/objects/pack/pack-*.idx &&
		packed_objects member/.git/objects/pack/pack-*.idx
	} | sort >expected-objects &&

	git -C member pack-objects --stdin-packs generated-pack <packfiles &&
	packed_objects member/generated-pack-*.idx >actual-objects &&
	test_cmp expected-objects actual-objects
'

test_done
