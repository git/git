#!/bin/sh

test_description='partial clone'

. ./test-lib.sh

delete_object () {
	rm $1/.git/objects/$(echo $2 | sed -e 's|^..|&/|')
}

pack_as_from_promisor () {
	HASH=$(git -C repo pack-objects .git/objects/pack/pack) &&
	>repo/.git/objects/pack/pack-$HASH.promisor &&
	echo $HASH
}

promise_and_delete () {
	HASH=$(git -C repo rev-parse "$1") &&
	git -C repo tag -a -m message my_annotated_tag "$HASH" &&
	git -C repo rev-parse my_annotated_tag | pack_as_from_promisor &&
	# tag -d prints a message to stdout, so redirect it
	git -C repo tag -d my_annotated_tag >/dev/null &&
	delete_object repo "$HASH"
}

test_expect_success 'extensions.partialclone without filter' '
	test_create_repo server &&
	git clone --filter="blob:none" "file://$(pwd)/server" client &&
	git -C client config --unset core.partialclonefilter &&
	git -C client fetch origin
'

test_expect_success 'missing reflog object, but promised by a commit, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo my_commit &&

	A=$(git -C repo commit-tree -m a HEAD^{tree}) &&
	C=$(git -C repo commit-tree -m c -p $A HEAD^{tree}) &&

	# Reference $A only from reflog, and delete it
	git -C repo branch my_branch "$A" &&
	git -C repo branch -f my_branch my_commit &&
	delete_object repo "$A" &&

	# State that we got $C, which refers to $A, from promisor
	printf "$C\n" | pack_as_from_promisor &&

	# Normally, it fails
	test_must_fail git -C repo fsck &&

	# But with the extension, it succeeds
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo fsck
'

test_expect_success 'missing reflog object, but promised by a tag, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo my_commit &&

	A=$(git -C repo commit-tree -m a HEAD^{tree}) &&
	git -C repo tag -a -m d my_tag_name $A &&
	T=$(git -C repo rev-parse my_tag_name) &&
	git -C repo tag -d my_tag_name &&

	# Reference $A only from reflog, and delete it
	git -C repo branch my_branch "$A" &&
	git -C repo branch -f my_branch my_commit &&
	delete_object repo "$A" &&

	# State that we got $T, which refers to $A, from promisor
	printf "$T\n" | pack_as_from_promisor &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo fsck
'

test_expect_success 'missing reflog object alone fails fsck, even with extension set' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo my_commit &&

	A=$(git -C repo commit-tree -m a HEAD^{tree}) &&
	B=$(git -C repo commit-tree -m b HEAD^{tree}) &&

	# Reference $A only from reflog, and delete it
	git -C repo branch my_branch "$A" &&
	git -C repo branch -f my_branch my_commit &&
	delete_object repo "$A" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	test_must_fail git -C repo fsck
'

test_expect_success 'missing ref object, but promised, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo my_commit &&

	A=$(git -C repo commit-tree -m a HEAD^{tree}) &&

	# Reference $A only from ref
	git -C repo branch my_branch "$A" &&
	promise_and_delete "$A" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo fsck
'

test_expect_success 'missing object, but promised, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo 1 &&
	test_commit -C repo 2 &&
	test_commit -C repo 3 &&
	git -C repo tag -a annotated_tag -m "annotated tag" &&

	C=$(git -C repo rev-parse 1) &&
	T=$(git -C repo rev-parse 2^{tree}) &&
	B=$(git hash-object repo/3.t) &&
	AT=$(git -C repo rev-parse annotated_tag) &&

	promise_and_delete "$C" &&
	promise_and_delete "$T" &&
	promise_and_delete "$B" &&
	promise_and_delete "$AT" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo fsck
'

test_expect_success 'missing CLI object, but promised, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo my_commit &&

	A=$(git -C repo commit-tree -m a HEAD^{tree}) &&
	promise_and_delete "$A" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo fsck "$A"
'

test_expect_success 'fetching of missing objects' '
	rm -rf repo &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	HASH=$(git -C repo rev-parse foo) &&
	rm -rf repo/.git/objects/* &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&
	git -C repo cat-file -p "$HASH" &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(cat promisorlist | sed "s/promisor$/idx/") &&
	git verify-pack --verbose "$IDX" | grep "$HASH"
'

test_expect_success 'fetching of missing objects works with ref-in-want enabled' '
	# ref-in-want requires protocol version 2
	git -C server config protocol.version 2 &&
	git -C server config uploadpack.allowrefinwant 1 &&
	git -C repo config protocol.version 2 &&

	rm -rf repo/.git/objects/* &&
	rm -f trace &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C repo cat-file -p "$HASH" &&
	grep "git< fetch=.*ref-in-want" trace
'

test_expect_success 'fetching of missing blobs works' '
	rm -rf server repo &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	git hash-object repo/foo.t >blobhash &&
	rm -rf repo/.git/objects/* &&

	git -C server config uploadpack.allowanysha1inwant 1 &&
	git -C server config uploadpack.allowfilter 1 &&
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&

	git -C repo cat-file -p $(cat blobhash)
'

test_expect_success 'fetching of missing trees does not fetch blobs' '
	rm -rf server repo &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	git -C repo rev-parse foo^{tree} >treehash &&
	git hash-object repo/foo.t >blobhash &&
	rm -rf repo/.git/objects/* &&

	git -C server config uploadpack.allowanysha1inwant 1 &&
	git -C server config uploadpack.allowfilter 1 &&
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&
	git -C repo cat-file -p $(cat treehash) &&

	# Ensure that the tree, but not the blob, is fetched
	git -C repo rev-list --objects --missing=print $(cat treehash) >objects &&
	grep "^$(cat treehash)" objects &&
	grep "^[?]$(cat blobhash)" objects
'

test_expect_success 'rev-list stops traversal at missing and promised commit' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo foo &&
	test_commit -C repo bar &&

	FOO=$(git -C repo rev-parse foo) &&
	promise_and_delete "$FOO" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	GIT_TEST_COMMIT_GRAPH=0 git -C repo rev-list --exclude-promisor-objects --objects bar >out &&
	grep $(git -C repo rev-parse bar) out &&
	! grep $FOO out
'

test_expect_success 'missing tree objects with --missing=allow-promisor and --exclude-promisor-objects' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo foo &&
	test_commit -C repo bar &&
	test_commit -C repo baz &&

	promise_and_delete $(git -C repo rev-parse bar^{tree}) &&
	promise_and_delete $(git -C repo rev-parse foo^{tree}) &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&

	git -C repo rev-list --missing=allow-promisor --objects HEAD >objs 2>rev_list_err &&
	test_must_be_empty rev_list_err &&
	# 3 commits, 3 blobs, and 1 tree
	test_line_count = 7 objs &&

	# Do the same for --exclude-promisor-objects, but with all trees gone.
	promise_and_delete $(git -C repo rev-parse baz^{tree}) &&
	git -C repo rev-list --exclude-promisor-objects --objects HEAD >objs 2>rev_list_err &&
	test_must_be_empty rev_list_err &&
	# 3 commits, no blobs or trees
	test_line_count = 3 objs
'

test_expect_success 'missing non-root tree object and rev-list' '
	rm -rf repo &&
	test_create_repo repo &&
	mkdir repo/dir &&
	echo foo >repo/dir/foo &&
	git -C repo add dir/foo &&
	git -C repo commit -m "commit dir/foo" &&

	promise_and_delete $(git -C repo rev-parse HEAD:dir) &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&

	git -C repo rev-list --missing=allow-any --objects HEAD >objs 2>rev_list_err &&
	test_must_be_empty rev_list_err &&
	# 1 commit and 1 tree
	test_line_count = 2 objs
'

test_expect_success 'rev-list stops traversal at missing and promised tree' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo foo &&
	mkdir repo/a_dir &&
	echo something >repo/a_dir/something &&
	git -C repo add a_dir/something &&
	git -C repo commit -m bar &&

	# foo^{tree} (tree referenced from commit)
	TREE=$(git -C repo rev-parse foo^{tree}) &&

	# a tree referenced by HEAD^{tree} (tree referenced from tree)
	TREE2=$(git -C repo ls-tree HEAD^{tree} | grep " tree " | head -1 | cut -b13-52) &&

	promise_and_delete "$TREE" &&
	promise_and_delete "$TREE2" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo rev-list --exclude-promisor-objects --objects HEAD >out &&
	grep $(git -C repo rev-parse foo) out &&
	! grep $TREE out &&
	grep $(git -C repo rev-parse HEAD) out &&
	! grep $TREE2 out
'

test_expect_success 'rev-list stops traversal at missing and promised blob' '
	rm -rf repo &&
	test_create_repo repo &&
	echo something >repo/something &&
	git -C repo add something &&
	git -C repo commit -m foo &&

	BLOB=$(git -C repo hash-object -w something) &&
	promise_and_delete "$BLOB" &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo rev-list --exclude-promisor-objects --objects HEAD >out &&
	grep $(git -C repo rev-parse HEAD) out &&
	! grep $BLOB out
'

test_expect_success 'rev-list stops traversal at promisor commit, tree, and blob' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo foo &&
	test_commit -C repo bar &&
	test_commit -C repo baz &&

	COMMIT=$(git -C repo rev-parse foo) &&
	TREE=$(git -C repo rev-parse bar^{tree}) &&
	BLOB=$(git hash-object repo/baz.t) &&
	printf "%s\n%s\n%s\n" $COMMIT $TREE $BLOB | pack_as_from_promisor &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo rev-list --exclude-promisor-objects --objects HEAD >out &&
	! grep $COMMIT out &&
	! grep $TREE out &&
	! grep $BLOB out &&
	grep $(git -C repo rev-parse bar) out  # sanity check that some walking was done
'

test_expect_success 'rev-list dies for missing objects on cmd line' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo foo &&
	test_commit -C repo bar &&
	test_commit -C repo baz &&

	COMMIT=$(git -C repo rev-parse foo) &&
	TREE=$(git -C repo rev-parse bar^{tree}) &&
	BLOB=$(git hash-object repo/baz.t) &&

	promise_and_delete $COMMIT &&
	promise_and_delete $TREE &&
	promise_and_delete $BLOB &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&

	for OBJ in "$COMMIT" "$TREE" "$BLOB"; do
		test_must_fail git -C repo rev-list --objects \
			--exclude-promisor-objects "$OBJ" &&
		test_must_fail git -C repo rev-list --objects-edge-aggressive \
			--exclude-promisor-objects "$OBJ" &&

		# Do not die or crash when --ignore-missing is passed.
		git -C repo rev-list --ignore-missing --objects \
			--exclude-promisor-objects "$OBJ" &&
		git -C repo rev-list --ignore-missing --objects-edge-aggressive \
			--exclude-promisor-objects "$OBJ"
	done
'

test_expect_success 'gc repacks promisor objects separately from non-promisor objects' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo one &&
	test_commit -C repo two &&

	TREE_ONE=$(git -C repo rev-parse one^{tree}) &&
	printf "$TREE_ONE\n" | pack_as_from_promisor &&
	TREE_TWO=$(git -C repo rev-parse two^{tree}) &&
	printf "$TREE_TWO\n" | pack_as_from_promisor &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo gc &&

	# Ensure that exactly one promisor packfile exists, and that it
	# contains the trees but not the commits
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	PROMISOR_PACKFILE=$(sed "s/.promisor/.pack/" <promisorlist) &&
	git verify-pack $PROMISOR_PACKFILE -v >out &&
	grep "$TREE_ONE" out &&
	grep "$TREE_TWO" out &&
	! grep "$(git -C repo rev-parse one)" out &&
	! grep "$(git -C repo rev-parse two)" out &&

	# Remove the promisor packfile and associated files
	rm $(sed "s/.promisor//" <promisorlist).* &&

	# Ensure that the single other pack contains the commits, but not the
	# trees
	ls repo/.git/objects/pack/pack-*.pack >packlist &&
	test_line_count = 1 packlist &&
	git verify-pack repo/.git/objects/pack/pack-*.pack -v >out &&
	grep "$(git -C repo rev-parse one)" out &&
	grep "$(git -C repo rev-parse two)" out &&
	! grep "$TREE_ONE" out &&
	! grep "$TREE_TWO" out
'

test_expect_success 'gc does not repack promisor objects if there are none' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo one &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo gc &&

	# Ensure that only one pack exists
	ls repo/.git/objects/pack/pack-*.pack >packlist &&
	test_line_count = 1 packlist
'

repack_and_check () {
	rm -rf repo2 &&
	cp -r repo repo2 &&
	git -C repo2 repack $1 -d &&
	git -C repo2 fsck &&

	git -C repo2 cat-file -e $2 &&
	git -C repo2 cat-file -e $3
}

test_expect_success 'repack -d does not irreversibly delete promisor objects' '
	rm -rf repo &&
	test_create_repo repo &&
	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&

	git -C repo commit --allow-empty -m one &&
	git -C repo commit --allow-empty -m two &&
	git -C repo commit --allow-empty -m three &&
	git -C repo commit --allow-empty -m four &&
	ONE=$(git -C repo rev-parse HEAD^^^) &&
	TWO=$(git -C repo rev-parse HEAD^^) &&
	THREE=$(git -C repo rev-parse HEAD^) &&

	printf "$TWO\n" | pack_as_from_promisor &&
	printf "$THREE\n" | pack_as_from_promisor &&
	delete_object repo "$ONE" &&

	repack_and_check -a "$TWO" "$THREE" &&
	repack_and_check -A "$TWO" "$THREE" &&
	repack_and_check -l "$TWO" "$THREE"
'

test_expect_success 'gc stops traversal when a missing but promised object is reached' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo my_commit &&

	TREE_HASH=$(git -C repo rev-parse HEAD^{tree}) &&
	HASH=$(promise_and_delete $TREE_HASH) &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&
	git -C repo gc &&

	# Ensure that the promisor packfile still exists, and remove it
	test -e repo/.git/objects/pack/pack-$HASH.pack &&
	rm repo/.git/objects/pack/pack-$HASH.* &&

	# Ensure that the single other pack contains the commit, but not the tree
	ls repo/.git/objects/pack/pack-*.pack >packlist &&
	test_line_count = 1 packlist &&
	git verify-pack repo/.git/objects/pack/pack-*.pack -v >out &&
	grep "$(git -C repo rev-parse HEAD)" out &&
	! grep "$TREE_HASH" out
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'fetching of missing objects from an HTTP server' '
	rm -rf repo &&
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	test_create_repo "$SERVER" &&
	test_commit -C "$SERVER" foo &&
	git -C "$SERVER" repack -a -d --write-bitmap-index &&

	git clone $HTTPD_URL/smart/server repo &&
	HASH=$(git -C repo rev-parse foo) &&
	rm -rf repo/.git/objects/* &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&
	git -C repo cat-file -p "$HASH" &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(cat promisorlist | sed "s/promisor$/idx/") &&
	git verify-pack --verbose "$IDX" | grep "$HASH"
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
