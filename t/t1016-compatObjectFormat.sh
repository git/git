#!/bin/sh
#
# Copyright (c) 2023 Eric Biederman
#

test_description='Test how well compatObjectFormat works'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

if ! test_have_prereq RUST
then
	skip_all='interoperability requires a Git built with Rust'
	test_done
fi

# All of the follow variables must be defined in the environment:
# GIT_AUTHOR_NAME
# GIT_AUTHOR_EMAIL
# GIT_AUTHOR_DATE
# GIT_COMMITTER_NAME
# GIT_COMMITTER_EMAIL
# GIT_COMMITTER_DATE
#
# The test relies on these variables being set so that the two
# different commits in two different repositories encoded with two
# different hash functions result in the same content in the commits.
# This means that when the commit is translated between hash functions
# the commit is identical to the commit in the other repository.
#
# Similarly this test relies on:
#	gpg --faked-system-time '20230918T154812!
# freezing the system time from gpg perspective so that two different
# runs of gpg applied to the same data result in identical signatures.
#

compat_hash () {
	case "$1" in
	"sha1")
		echo "sha256"
		;;
	"sha256")
		echo "sha1"
		;;
	esac
}

create_compat_repo () {
	local hash="$1"
	local repo="$2"

	git init --object-format="$hash" "$repo" &&
	git -C "$repo" config set core.repositoryformatversion 1 &&
	git -C "$repo" config set extensions.compatObjectFormat $(compat_hash "$hash")
}

hello_oid () {
	case "$1" in
	"sha1")
		echo "$hello_sha1_oid"
		;;
	"sha256")
		echo "$hello_sha256_oid"
		;;
	esac
}

tree_oid () {
	case "$1" in
	"sha1")
		echo "$tree_sha1_oid"
		;;
	"sha256")
		echo "$tree_sha256_oid"
		;;
	esac
}

commit_oid () {
	case "$1" in
	"sha1")
		echo "$commit_sha1_oid"
		;;
	"sha256")
		echo "$commit_sha256_oid"
		;;
	esac
}

commit2_oid () {
	case "$1" in
	"sha1")
		echo "$commit2_sha1_oid"
		;;
	"sha256")
		echo "$commit2_sha256_oid"
		;;
	esac
}

del_sigcommit () {
	local delete="$1"

	if test "$delete" = "sha256" ; then
		local pattern="gpgsig-sha256"
	else
		local pattern="gpgsig"
	fi
	test-tool delete-gpgsig "$pattern"
}

del_sigtag () {
	local storage="$1"
	local delete="$2"

	if test "$storage" = "$delete" ; then
		local pattern="trailer"
	elif test "$storage" = "sha256" ; then
		local pattern="gpgsig"
	else
		local pattern="gpgsig-sha256"
	fi
	test-tool delete-gpgsig "$pattern"
}

base=$(pwd)
for hash in sha1 sha256
do
	cd "$base"
	mkdir -p repo-$hash
	cd repo-$hash

	test_expect_success "setup $hash repository" '
		git init --object-format=$hash &&
		git config core.repositoryformatversion 1 &&
		git config extensions.objectformat $hash &&
		git config extensions.compatobjectformat $(compat_hash $hash) &&
		git config gpg.program $TEST_DIRECTORY/t1016/gpg &&
		echo "Hello World!" >hello &&
		eval hello_${hash}_oid=$(git hash-object hello) &&
		git update-index --add hello &&
		git commit -m "Initial commit" &&
		eval commit_${hash}_oid=$(git rev-parse HEAD) &&
		eval tree_${hash}_oid=$(git rev-parse HEAD^{tree})
	'
	test_expect_success "create a $hash  tagged blob" '
		git tag --no-sign -m "This is a tag" hellotag $(hello_oid $hash) &&
		eval hellotag_${hash}_oid=$(git rev-parse hellotag)
	'
	test_expect_success "create a $hash tagged tree" '
		git tag --no-sign -m "This is a tag" treetag $(tree_oid $hash) &&
		eval treetag_${hash}_oid=$(git rev-parse treetag)
	'
	test_expect_success "create a $hash tagged commit" '
		git tag --no-sign -m "This is a tag" committag $(commit_oid $hash) &&
		eval committag_${hash}_oid=$(git rev-parse committag)
	'
	test_expect_success GPG2 "create a $hash signed commit" '
		git commit --gpg-sign --allow-empty -m "This is a signed commit" &&
		eval signedcommit_${hash}_oid=$(git rev-parse HEAD)
	'
	test_expect_success GPG2 "create a $hash signed tag" '
		git tag -s -m "This is a signed tag" signedtag HEAD &&
		eval signedtag_${hash}_oid=$(git rev-parse signedtag)
	'
	test_expect_success "create a $hash branch" '
		git checkout -b branch $(commit_oid $hash) &&
		echo "More more more give me more!" >more &&
		eval more_${hash}_oid=$(git hash-object more) &&
		echo "Another and another and another" >another &&
		eval another_${hash}_oid=$(git hash-object another) &&
		git update-index --add more another &&
		git commit -m "Add more files!" &&
		eval commit2_${hash}_oid=$(git rev-parse HEAD) &&
		eval tree2_${hash}_oid=$(git rev-parse HEAD^{tree})
	'
	test_expect_success GPG2 "create another $hash signed tag" '
		git tag -s -m "This is another signed tag" signedtag2 $(commit2_oid $hash) &&
		eval signedtag2_${hash}_oid=$(git rev-parse signedtag2)
	'
	test_expect_success GPG2 "merge the $hash branches together" '
		git merge -S -m "merge some signed tags together" signedtag signedtag2 &&
		eval signedcommit2_${hash}_oid=$(git rev-parse HEAD)
	'
	test_expect_success GPG2 "create additional $hash signed commits" '
		git commit --gpg-sign --allow-empty -m "This is an additional signed commit" &&
		git cat-file commit HEAD | del_sigcommit sha256 >"../${hash}_signedcommit3" &&
		git cat-file commit HEAD | del_sigcommit sha1 >"../${hash}_signedcommit4" &&
		eval signedcommit3_${hash}_oid=$(git hash-object -t commit -w ../${hash}_signedcommit3) &&
		eval signedcommit4_${hash}_oid=$(git hash-object -t commit -w ../${hash}_signedcommit4)
	'
	test_expect_success GPG2 "create additional $hash signed tags" '
		git tag -s -m "This is an additional signed tag" signedtag34 HEAD &&
		git cat-file tag signedtag34 | del_sigtag "${hash}" sha256 >../${hash}_signedtag3 &&
		git cat-file tag signedtag34 | del_sigtag "${hash}" sha1 >../${hash}_signedtag4 &&
		eval signedtag3_${hash}_oid=$(git hash-object -t tag -w ../${hash}_signedtag3) &&
		eval signedtag4_${hash}_oid=$(git hash-object -t tag -w ../${hash}_signedtag4)
	'

	test_expect_success 'rev-parse maps oid of object borrowed from alternate' '
		for repo in alt borrow
		do
			test_when_finished "rm -rf $repo" &&
			create_compat_repo $hash $repo || exit 1
		done &&

		git -C alt commit --allow-empty --message A &&
		echo "$(pwd)/alt/.git/objects" >borrow/.git/objects/info/alternates &&

		oid=$(git -C alt rev-parse HEAD) &&
		git -C alt    rev-parse --output-object-format=$(compat_hash $hash) "$oid" >expect &&
		git -C borrow rev-parse --output-object-format=$(compat_hash $hash) "$oid" >actual &&
		test_cmp expect actual
	'

	test_expect_success 'index-pack writes pack index v3' '
		test_when_finished "rm -rf v3-repo v3.idx" &&
		create_compat_repo $hash v3-repo &&
		echo content >v3-repo/file &&
		git -C v3-repo add file &&
		git -C v3-repo commit --message A &&
		git -C v3-repo commit --allow-empty --message B &&
		git -C v3-repo repack -a -d &&
		pack=$(echo v3-repo/.git/objects/pack/pack-*.pack) &&
		git -C v3-repo index-pack --index-version=3 \
			-o "$(pwd)/v3.idx" "$(pwd)/$pack" &&
		echo "ff 74 4f 63 00 00 00 03 " >expect &&
		test_copy_bytes 8 <v3.idx | test-tool hexdump >actual &&
		test_cmp expect actual
	'

	test_expect_success 'add a large file streamed into a pack' '
		test_when_finished "rm -rf large-repo" &&
		create_compat_repo $hash large-repo &&
		git -C large-repo config set core.bigFileThreshold 1k &&
		test-tool genrandom large 5000 >large-repo/large &&
		git -C large-repo add large &&
		ls large-repo/.git/objects/pack/pack-*.pack &&
		git -C large-repo hash-object large >expect &&
		git -C large-repo rev-parse :large >actual &&
		test_cmp expect actual &&
		git -C large-repo cat-file blob :large >actual &&
		test_cmp large-repo/large actual
	'

	test_expect_success 'packed objects are only mapped using pack index v3' '
		test_when_finished "rm -rf map-repo v3.idx loose-object-idx" &&
		create_compat_repo $hash map-repo &&
		echo content >map-repo/file &&
		git -C map-repo add file &&
		git -C map-repo commit --message A &&
		oid=$(git -C map-repo rev-parse HEAD) &&
		git -C map-repo rev-parse --output-object-format=$(compat_hash $hash) "$oid" >expect &&
		git -C map-repo repack -a -d &&
		pack=$(echo map-repo/.git/objects/pack/pack-*.pack) &&
		git -C map-repo index-pack --index-version=3 \
			-o "$(pwd)/v3.idx" "$(pwd)/$pack" &&
		mv map-repo/.git/objects/loose-object-idx loose-object-idx &&

		# A v2 index only has object IDs in the main algorithm, so it
		# must not provide a mapping.
		git -C map-repo rev-parse --output-object-format=$(compat_hash $hash) "$oid" >actual &&
		! test_cmp expect actual &&
		test $(wc -c <actual) -ne $(($(test_oid --hash=$(compat_hash $hash) hexsz) + 1)) &&

		# A v3 index provides it.
		rm -f "${pack%.pack}.idx" "${pack%.pack}.rev" &&
		mv v3.idx "${pack%.pack}.idx" &&
		git -C map-repo rev-parse --output-object-format=$(compat_hash $hash) "$oid" >actual &&
		test_cmp expect actual
	'

	test_expect_success 'abbreviated compat object IDs are found using pack index v3' '
		test_when_finished "rm -rf abbrev-repo v3.idx" &&
		create_compat_repo $hash abbrev-repo &&
		echo content >abbrev-repo/file &&
		git -C abbrev-repo add file &&
		git -C abbrev-repo commit --message A &&
		compat_oid=$(git -C abbrev-repo rev-parse --output-object-format=$(compat_hash $hash) HEAD) &&
		abbrev=$(echo $compat_oid | cut -c 1-12) &&

		# A multi-pack-index only stores object IDs in the main
		# algorithm.
		GIT_TEST_MULTI_PACK_INDEX=0 git -C abbrev-repo repack -a -d &&
		pack=$(echo abbrev-repo/.git/objects/pack/pack-*.pack) &&
		git -C abbrev-repo index-pack --index-version=3 \
			-o "$(pwd)/v3.idx" "$(pwd)/$pack" &&
		rm abbrev-repo/.git/objects/loose-object-idx &&

		# A v2 index only has object IDs in the main algorithm.
		test_must_fail git -C abbrev-repo cat-file -t $abbrev &&

		rm -f "${pack%.pack}.idx" "${pack%.pack}.rev" &&
		mv v3.idx "${pack%.pack}.idx" &&
		echo commit >expect &&
		git -C abbrev-repo cat-file -t $abbrev >actual &&
		test_cmp expect actual &&
		echo $compat_oid >expect &&
		git -C abbrev-repo rev-parse --output-object-format=$(compat_hash $hash) $abbrev >actual &&
		test_cmp expect actual
	'
done
cd "$base"

compare_oids () {
	test "$#" = 5 && { local PREREQ="$1"; shift; } || PREREQ=
	local type="$1"
	local name="$2"
	local sha1_oid="$3"
	local sha256_oid="$4"

	echo ${sha1_oid} >${name}_sha1_expected
	echo ${sha256_oid} >${name}_sha256_expected
	echo ${type} >${name}_type_expected

	git --git-dir=repo-sha1/.git rev-parse --output-object-format=sha256 ${sha1_oid} >${name}_sha1_sha256_found
	git --git-dir=repo-sha256/.git rev-parse --output-object-format=sha1 ${sha256_oid} >${name}_sha256_sha1_found
	local sha1_sha256_oid="$(cat ${name}_sha1_sha256_found)"
	local sha256_sha1_oid="$(cat ${name}_sha256_sha1_found)"

	test_expect_success $PREREQ "Verify ${type} ${name}'s sha1 oid" '
		git --git-dir=repo-sha256/.git rev-parse --output-object-format=sha1 ${sha256_oid} >${name}_sha1 &&
		test_cmp ${name}_sha1 ${name}_sha1_expected
	'

	test_expect_success $PREREQ "Verify ${type} ${name}'s sha256 oid" '
		git --git-dir=repo-sha1/.git rev-parse --output-object-format=sha256 ${sha1_oid} >${name}_sha256 &&
		test_cmp ${name}_sha256 ${name}_sha256_expected
	'

	test_expect_success $PREREQ "Verify ${name}'s sha1 type" '
		git --git-dir=repo-sha1/.git cat-file -t ${sha1_oid} >${name}_type1 &&
		git --git-dir=repo-sha256/.git cat-file -t ${sha256_sha1_oid} >${name}_type2 &&
		test_cmp ${name}_type1 ${name}_type2 &&
		test_cmp ${name}_type1 ${name}_type_expected
	'

	test_expect_success $PREREQ "Verify ${name}'s sha256 type" '
		git --git-dir=repo-sha256/.git cat-file -t ${sha256_oid} >${name}_type3 &&
		git --git-dir=repo-sha1/.git cat-file -t ${sha1_sha256_oid} >${name}_type4 &&
		test_cmp ${name}_type3 ${name}_type4 &&
		test_cmp ${name}_type3 ${name}_type_expected
	'

	test_expect_success $PREREQ "Verify ${name}'s sha1 size" '
		git --git-dir=repo-sha1/.git cat-file -s ${sha1_oid} >${name}_size1 &&
		git --git-dir=repo-sha256/.git cat-file -s ${sha256_sha1_oid} >${name}_size2 &&
		test_cmp ${name}_size1 ${name}_size2
	'

	test_expect_success $PREREQ "Verify ${name}'s sha256 size" '
		git --git-dir=repo-sha256/.git cat-file -s ${sha256_oid} >${name}_size3 &&
		git --git-dir=repo-sha1/.git cat-file -s ${sha1_sha256_oid} >${name}_size4 &&
		test_cmp ${name}_size3 ${name}_size4
	'

	test_expect_success $PREREQ "Verify ${name}'s sha1 pretty content" '
		git --git-dir=repo-sha1/.git cat-file -p ${sha1_oid} >${name}_content1 &&
		git --git-dir=repo-sha256/.git cat-file -p ${sha256_sha1_oid} >${name}_content2 &&
		test_cmp ${name}_content1 ${name}_content2
	'

	test_expect_success $PREREQ "Verify ${name}'s sha256 pretty content" '
		git --git-dir=repo-sha256/.git cat-file -p ${sha256_oid} >${name}_content3 &&
		git --git-dir=repo-sha1/.git cat-file -p ${sha1_sha256_oid} >${name}_content4 &&
		test_cmp ${name}_content3 ${name}_content4
	'

	test_expect_success $PREREQ "Verify ${name}'s sha1 content" '
		git --git-dir=repo-sha1/.git cat-file ${type} ${sha1_oid} >${name}_content5 &&
		git --git-dir=repo-sha256/.git cat-file ${type} ${sha256_sha1_oid} >${name}_content6 &&
		test_cmp ${name}_content5 ${name}_content6
	'

	test_expect_success $PREREQ "Verify ${name}'s sha256 content" '
		git --git-dir=repo-sha256/.git cat-file ${type} ${sha256_oid} >${name}_content7 &&
		git --git-dir=repo-sha1/.git cat-file ${type} ${sha1_sha256_oid} >${name}_content8 &&
		test_cmp ${name}_content7 ${name}_content8
	'
}

compare_oids 'blob' hello "$hello_sha1_oid" "$hello_sha256_oid"
compare_oids 'tree' tree "$tree_sha1_oid" "$tree_sha256_oid"
compare_oids 'commit' commit "$commit_sha1_oid" "$commit_sha256_oid"
compare_oids GPG2 'commit' signedcommit "$signedcommit_sha1_oid" "$signedcommit_sha256_oid"
compare_oids 'tag' hellotag "$hellotag_sha1_oid" "$hellotag_sha256_oid"
compare_oids 'tag' treetag "$treetag_sha1_oid" "$treetag_sha256_oid"
compare_oids 'tag' committag "$committag_sha1_oid" "$committag_sha256_oid"
compare_oids GPG2 'tag' signedtag "$signedtag_sha1_oid" "$signedtag_sha256_oid"

compare_oids 'blob' more "$more_sha1_oid" "$more_sha256_oid"
compare_oids 'blob' another "$another_sha1_oid" "$another_sha256_oid"
compare_oids 'tree' tree2 "$tree2_sha1_oid" "$tree2_sha256_oid"
compare_oids 'commit' commit2 "$commit2_sha1_oid" "$commit2_sha256_oid"
compare_oids GPG2 'tag' signedtag2 "$signedtag2_sha1_oid" "$signedtag2_sha256_oid"
compare_oids GPG2 'commit' signedcommit2 "$signedcommit2_sha1_oid" "$signedcommit2_sha256_oid"
compare_oids GPG2 'commit' signedcommit3 "$signedcommit3_sha1_oid" "$signedcommit3_sha256_oid"
compare_oids GPG2 'commit' signedcommit4 "$signedcommit4_sha1_oid" "$signedcommit4_sha256_oid"
compare_oids GPG2 'tag' signedtag3 "$signedtag3_sha1_oid" "$signedtag3_sha256_oid"
compare_oids GPG2 'tag' signedtag4 "$signedtag4_sha1_oid" "$signedtag4_sha256_oid"

test_expect_success 'batched fsync migrates loose object index' '
	test_when_finished "rm -rf batch-fsync" &&
	git init --object-format=sha1 batch-fsync &&
	(
		cd batch-fsync &&
		git config core.repositoryFormatVersion 1 &&
		git config extensions.compatObjectFormat sha256 &&
		git config core.fsync loose-object &&
		git config core.fsyncMethod batch &&

		# Write object to generate initial loose object index.
		echo foo >foo &&
		git add foo &&
		test_grep "$(git rev-parse :foo)" .git/objects/loose-object-idx &&

		# Write some additional objects and ensure the corresponding
		# temporary loose object index entries stored in the transaction
		# are merged into the primary object directory.
		echo bar >bar &&
		echo baz >baz &&
		git add bar baz &&
		test_grep "$(git rev-parse :foo)" .git/objects/loose-object-idx &&
		test_grep "$(git rev-parse :bar)" .git/objects/loose-object-idx &&
		test_grep "$(git rev-parse :baz)" .git/objects/loose-object-idx
	)
'

test_done
