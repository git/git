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
			git init --object-format=$hash $repo &&
			git -C $repo config set core.repositoryformatversion 1 &&
			git -C $repo config set extensions.compatObjectFormat $(compat_hash $hash) || exit 1
		done &&

		git -C alt commit --allow-empty --message A &&
		echo "$(pwd)/alt/.git/objects" >borrow/.git/objects/info/alternates &&

		oid=$(git -C alt rev-parse HEAD) &&
		git -C alt    rev-parse --output-object-format=$(compat_hash $hash) "$oid" >expect &&
		git -C borrow rev-parse --output-object-format=$(compat_hash $hash) "$oid" >actual &&
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

test_expect_success 'setup repos whose history predates the compat extension' '
	git init pre-sha1 &&
	git init --object-format=sha256 pre-sha256 &&
	for repo in pre-sha1 pre-sha256
	do
		mkdir $repo/sub &&
		echo one >$repo/sub/file-one &&
		git -C $repo add . &&
		git -C $repo commit -m "initial commit" &&
		echo two >$repo/file-two &&
		git -C $repo add . &&
		git -C $repo commit -m "second commit" &&
		git -C $repo tag -m "a tag" mytag || return 1
	done
'

test_expect_success 'pack the history before enabling the compat extension' '
	git -C pre-sha256 repack -a -d
'

test_expect_success 'compat names are unknown before they are recorded' '
	git -C pre-sha256 config core.repositoryformatversion 1 &&
	git -C pre-sha256 config extensions.compatObjectFormat sha1 &&
	test_must_fail git -C pre-sha256 cat-file -t \
		$(git -C pre-sha1 rev-parse HEAD^{tree})
'

test_expect_success 'derive compat names when packing the objects' '
	echo three >pre-sha256/file-three &&
	git -C pre-sha256 add file-three &&
	git -C pre-sha256 commit -m "third commit" &&
	git -C pre-sha256 repack -a -d
'

test_expect_success 'derived compat names name the right objects' '
	for name in HEAD HEAD^ HEAD^{tree} mytag; do
		oid=$(git -C pre-sha1 rev-parse $name) &&
		git -C pre-sha1 cat-file -t $oid >expect &&
		git -C pre-sha256 cat-file -t $oid >actual &&
		test_cmp expect actual || return 1
	done
'

test_expect_success 'derived compat names translate to the same content' '
	for name in HEAD HEAD^ HEAD^{tree} mytag; do
		oid=$(git -C pre-sha1 rev-parse $name) &&
		type=$(git -C pre-sha1 cat-file -t $oid) &&
		git -C pre-sha1 cat-file $type $oid >expect &&
		git -C pre-sha256 cat-file $type $oid >actual &&
		test_cmp expect actual || return 1
	done
'

test_expect_success 'setup for push tests' '
	git init --bare push-remote.git &&
	git init --object-format=sha256 push-local &&
	git -C push-local config core.repositoryformatversion 1 &&
	git -C push-local config extensions.compatObjectFormat sha1 &&
	echo one >push-local/file-one &&
	git -C push-local add file-one &&
	git -C push-local commit -m "initial commit" &&
	echo two >push-local/file-two &&
	git -C push-local add file-two &&
	git -C push-local commit -m "second commit" &&
	git -C push-local tag -m "a tag" mytag &&
	git -C push-local remote add origin "$PWD/push-remote.git"
'

test_expect_success 'push a branch to a remote using the compat object format' '
	git -C push-local push origin master:refs/heads/master &&
	git -C push-local rev-parse --output-object-format=sha1 master >expect &&
	git -C push-remote.git rev-parse refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'the remote can read the objects we pushed' '
	echo two >expect &&
	git -C push-remote.git cat-file blob refs/heads/master:file-two >actual &&
	test_cmp expect actual &&
	git -C push-remote.git fsck --strict >actual 2>err &&
	test_must_be_empty err
'

test_expect_success 'push an annotated tag to a remote using the compat object format' '
	git -C push-local push origin mytag:refs/tags/mytag &&
	git -C push-local rev-parse --output-object-format=sha1 mytag >expect &&
	git -C push-remote.git rev-parse refs/tags/mytag >actual &&
	test_cmp expect actual
'

test_expect_success 'pushing again is a no-op' '
	git -C push-local push origin master:refs/heads/master >out 2>&1 &&
	test_grep "Everything up-to-date" out
'

test_expect_success 'a non-fast-forward push is rejected' '
	git -C push-local reset --hard HEAD~1 &&
	echo three >push-local/file-three &&
	git -C push-local add file-three &&
	git -C push-local commit -m "third commit" &&
	test_must_fail git -C push-local push origin master:refs/heads/master 2>err &&
	test_grep "non-fast-forward" err
'

test_expect_success 'a forced push updates the remote' '
	git -C push-local push --force origin master:refs/heads/master &&
	git -C push-local rev-parse --output-object-format=sha1 master >expect &&
	git -C push-remote.git rev-parse refs/heads/master >actual &&
	test_cmp expect actual &&
	git -C push-remote.git fsck --strict >actual 2>err &&
	test_must_be_empty err
'

test_expect_success 'push after the objects have been packed' '
	git -C push-local gc &&
	git -C push-local checkout -b side &&
	echo four >push-local/file-four &&
	git -C push-local add file-four &&
	git -C push-local commit -m "fourth commit" &&
	git -C push-local push origin side:refs/heads/side &&
	git -C push-local rev-parse --output-object-format=sha1 side >expect &&
	git -C push-remote.git rev-parse refs/heads/side >actual &&
	test_cmp expect actual &&
	git -C push-remote.git fsck --strict >actual 2>err &&
	test_must_be_empty err
'

test_expect_success 'delete a reference on a remote using the compat object format' '
	git -C push-local push origin :refs/heads/side &&
	test_must_fail git -C push-remote.git rev-parse --verify refs/heads/side
'

test_expect_success 'packfile names objects in the requested object format' '
	git init --bare pack-verify.git &&
	git -C push-local rev-list --objects --all >objects &&
	git -C push-local pack-objects --stdout --output-object-format=sha1 <objects >pack &&
	git -C pack-verify.git index-pack --stdin <pack >actual 2>err &&
	test_must_be_empty err &&
	git -C pack-verify.git cat-file --batch-all-objects --batch-check="%(objecttype)" >actual &&
	git -C push-local cat-file --batch-all-objects --batch-check="%(objecttype)" >expect &&
	sort actual >actual.sorted &&
	sort expect >expect.sorted &&
	test_cmp expect.sorted actual.sorted
'

test_expect_success 'setup for compatibility names file tests' '
	git init names-sha1 &&
	git init --object-format=sha256 names-sha256 &&
	for repo in names-sha1 names-sha256
	do
		mkdir $repo/sub &&
		echo one >$repo/sub/file-one &&
		git -C $repo add . &&
		git -C $repo commit -m "initial commit" &&
		echo two >$repo/file-two &&
		git -C $repo add . &&
		git -C $repo commit -m "second commit" &&
		git -C $repo tag -m "a tag" mytag || return 1
	done &&
	git -C names-sha256 config core.repositoryformatversion 1 &&
	git -C names-sha256 config extensions.compatObjectFormat sha1 &&
	git -C names-sha256 repack -a -d
'

test_expect_success 'a compatibility names file is written next to the pack' '
	ls names-sha256/.git/objects/pack/*.compat >actual &&
	test $(wc -l <actual) = 1
'

test_expect_success 'the loose object map is not needed to name packed objects' '
	rm names-sha256/.git/objects/loose-object-idx &&
	for name in HEAD HEAD^ HEAD^{tree} mytag
	do
		oid=$(git -C names-sha1 rev-parse $name) &&
		git -C names-sha1 cat-file -t $oid >expect &&
		git -C names-sha256 cat-file -t $oid >actual &&
		test_cmp expect actual || return 1
	done
'

test_expect_success 'packed objects are reachable by their compat name' '
	git -C names-sha256 log --format=%s $(git -C names-sha1 rev-parse HEAD^) >actual &&
	git -C names-sha1 log --format=%s HEAD^ >expect &&
	test_cmp expect actual
'

test_expect_success 'compat names are still derived for objects without a map' '
	echo three >names-sha256/file-three &&
	git -C names-sha256 add file-three &&
	git -C names-sha256 commit -m "third commit" &&
	git -C names-sha256 rev-parse --output-object-format=sha1 HEAD >compat_name &&
	git -C names-sha256 cat-file commit $(cat compat_name) >actual &&
	git -C names-sha256 cat-file -t $(cat compat_name) >actual.type &&
	echo commit >expect.type &&
	test_cmp expect.type actual.type &&
	grep "^third commit$" actual
'

test_expect_success 'pruning does not leave compatibility names files behind' '
	git -C names-sha256 reflog expire --expire=now --all &&
	git -C names-sha256 prune --expire=now &&
	nr_idx=$(ls names-sha256/.git/objects/pack/*.idx | wc -l) &&
	nr_compat=$(ls names-sha256/.git/objects/pack/*.compat 2>/dev/null | wc -l) &&
	test $nr_idx -eq $nr_compat
'

test_expect_success 'setup for fetch tests' '
	git init --bare fetch-remote.git &&
	git init fetch-upstream &&
	for repo in fetch-upstream
	do
		for i in 1 2 3
		do
			echo $i >$repo/file-$i &&
			git -C $repo add . &&
			git -C $repo commit -m "commit $i" || return 1
		done &&
		git -C $repo tag -m "a tag" mytag &&
		git -C $repo push -q ../fetch-remote.git master mytag || return 1
	done &&
	git init --object-format=sha256 fetch-local &&
	git -C fetch-local config core.repositoryformatversion 1 &&
	git -C fetch-local config extensions.compatObjectFormat sha1 &&
	git -C fetch-local remote add origin "$PWD/fetch-remote.git"
'

test_expect_success 'fetch from a remote using the compat object format' '
	git -C fetch-local fetch origin &&
	git -C fetch-local rev-parse FETCH_HEAD >actual &&
	git -C fetch-upstream rev-parse master >expect &&
	git -C fetch-local rev-parse --output-object-format=sha1 FETCH_HEAD >sha1 &&
	test_cmp expect sha1
'

test_expect_success 'the fetched history matches the upstream repository' '
	git -C fetch-local log --format=%s FETCH_HEAD >actual &&
	git -C fetch-upstream log --format=%s master >expect &&
	test_cmp expect actual &&
	git -C fetch-local fsck --strict >fsck.out 2>fsck.err &&
	! grep -E "^(error|missing|broken)" fsck.out fsck.err
'

test_expect_success 'fetched objects are named in the compat object format' '
	git -C fetch-local rev-parse --output-object-format=sha1 FETCH_HEAD:file-2 >actual &&
	git -C fetch-upstream rev-parse master:file-2 >expect &&
	test_cmp expect actual &&
	git -C fetch-local cat-file -t $(cat actual) >type &&
	echo blob >expect.type &&
	test_cmp expect.type type
'

test_expect_success 'an incremental fetch works' '
	echo four >fetch-upstream/file-4 &&
	git -C fetch-upstream add file-4 &&
	git -C fetch-upstream commit -m "commit 4" &&
	git -C fetch-upstream push -q ../fetch-remote.git master &&
	git -C fetch-local fetch origin &&
	git -C fetch-local log --format=%s FETCH_HEAD >actual &&
	git -C fetch-upstream log --format=%s master >expect &&
	test_cmp expect actual
'

test_expect_success 'fetched objects can be pushed back' '
	git -C fetch-local fetch origin &&
	git -C fetch-local checkout -q -b roundtrip FETCH_HEAD &&
	echo five >fetch-local/file-5 &&
	git -C fetch-local add file-5 &&
	git -C fetch-local commit -m "commit 5" &&
	git -C fetch-local push origin roundtrip:refs/heads/roundtrip &&
	git -C fetch-remote.git rev-parse refs/heads/roundtrip >actual &&
	git -C fetch-local rev-parse --output-object-format=sha1 roundtrip >expect &&
	test_cmp expect actual &&
	git -C fetch-remote.git fsck --strict >fsck.out 2>fsck.err &&
	! grep -E "^(error|missing|broken)" fsck.out fsck.err
'

test_expect_success 'fetching into a repository without the compat extension fails' '
	git init --object-format=sha256 fetch-plain &&
	test_must_fail git -C fetch-plain fetch "$PWD/fetch-remote.git" master 2>err &&
	test_grep "does not support our object format" err
'

test_expect_success 'setup for index-pack tests' '
	git init pack-src &&
	for i in 1 2 3 4 5
	do
		echo $i >pack-src/file-$i &&
		git -C pack-src add . &&
		git -C pack-src commit -m "commit $i" || return 1
	done &&
	git -C pack-src tag -m "a tag" mytag &&
	git -C pack-src rev-list --objects --all >objects &&
	git -C pack-src pack-objects --stdout --delta-base-offset <objects >in.pack
'

test_expect_success 'index a packfile in the compat object format' '
	git init --object-format=sha256 pack-dst &&
	git -C pack-dst config core.repositoryformatversion 1 &&
	git -C pack-dst config extensions.compatObjectFormat sha1 &&
	git -C pack-dst index-pack --stdin --input-object-format=sha1 <in.pack >actual &&
	cut -f1 actual >actual.pack &&
	echo pack >expect.pack &&
	test_cmp expect.pack actual.pack
'

test_expect_success 'the indexed objects are named in the compat object format' '
	for oid in $(git -C pack-src cat-file --batch-all-objects --batch-check="%(objectname)")
	do
		git -C pack-dst cat-file -t $oid >/dev/null || return 1
	done
'

test_expect_success 'the indexed objects translate to the same content' '
	oid=$(git -C pack-src rev-parse HEAD) &&
	git -C pack-src cat-file commit HEAD >expect &&
	git -C pack-dst cat-file commit $oid >actual &&
	test_cmp expect actual &&
	git -C pack-dst fsck --strict >fsck.out 2>fsck.err &&
	! grep -E "^(error|missing|broken)" fsck.out fsck.err
'

test_expect_success 'a fetched packfile too large to unpack can be indexed' '
	echo six >pack-src/file-6 &&
	git -C pack-src add file-6 &&
	git -C pack-src commit -m "commit 6" &&
	git -C pack-src push -q ../fetch-remote.git master:refs/heads/packed &&
	git -c fetch.unpackLimit=1 -C fetch-local fetch origin packed &&
	git -C fetch-local log --format=%s FETCH_HEAD >actual &&
	git -C pack-src log --format=%s master >expect &&
	test_cmp expect actual &&
	git -C fetch-local fsck --strict >fsck.out 2>fsck.err &&
	! grep -E "^(error|missing|broken)" fsck.out fsck.err
'

test_expect_success 'a fetch that deepens is refused with a clear message' '
	echo deepen >fetch-upstream/file-7 &&
	git -C fetch-upstream add file-7 &&
	git -C fetch-upstream commit -m "commit 7" &&
	git -C fetch-upstream push -q ../fetch-remote.git master &&
	test_must_fail git -C fetch-local fetch --depth 1 origin master 2>err &&
	test_grep "cannot deepen a shallow repository" err
'

test_expect_success 'a fetch from a remote without the compat extension still fails' '
	git init --object-format=sha256 fetch-strict &&
	test_must_fail git -C fetch-strict fetch "$PWD/fetch-remote.git" master 2>err &&
	test_grep "does not support our object format" err
'

test_done
