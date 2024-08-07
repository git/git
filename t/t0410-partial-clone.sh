#!/bin/sh

test_description='partial clone'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

# missing promisor objects cause repacks which write bitmaps to fail
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0
# When enabled, some commands will write commit-graphs. This causes fsck
# to fail when delete_object() is called because fsck will attempt to
# verify the out-of-sync commit graph.
GIT_TEST_COMMIT_GRAPH=0

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
	git -C client config --unset remote.origin.partialclonefilter &&
	git -C client fetch origin
'

test_expect_success 'convert shallow clone to partial clone' '
	rm -fr server client &&
	test_create_repo server &&
	test_commit -C server my_commit 1 &&
	test_commit -C server my_commit2 1 &&
	git clone --depth=1 "file://$(pwd)/server" client &&
	git -C client fetch --unshallow --filter="blob:none" &&
	test_cmp_config -C client true remote.origin.promisor &&
	test_cmp_config -C client blob:none remote.origin.partialclonefilter &&
	test_cmp_config -C client 1 core.repositoryformatversion
'

test_expect_success DEFAULT_REPO_FORMAT 'convert to partial clone with noop extension' '
	rm -fr server client &&
	test_create_repo server &&
	test_commit -C server my_commit 1 &&
	test_commit -C server my_commit2 1 &&
	git clone --depth=1 "file://$(pwd)/server" client &&
	test_cmp_config -C client 0 core.repositoryformatversion &&
	git -C client config extensions.noop true &&
	git -C client fetch --unshallow --filter="blob:none"
'

test_expect_success DEFAULT_REPO_FORMAT 'converting to partial clone fails with unrecognized extension' '
	rm -fr server client &&
	test_create_repo server &&
	test_commit -C server my_commit 1 &&
	test_commit -C server my_commit2 1 &&
	git clone --depth=1 "file://$(pwd)/server" client &&
	test_cmp_config -C client 0 core.repositoryformatversion &&
	git -C client config extensions.nonsense true &&
	test_must_fail git -C client fetch --unshallow --filter="blob:none"
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
	rm -rf repo err &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server repack -a -d --write-bitmap-index &&

	git clone "file://$(pwd)/server" repo &&
	HASH=$(git -C repo rev-parse foo) &&
	rm -rf repo/.git/objects/* &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "origin" &&
	git -C repo cat-file -p "$HASH" 2>err &&

	# Ensure that no spurious FETCH_HEAD messages are written
	! grep FETCH_HEAD err &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH" out
'

test_expect_success 'fetching of a promised object that promisor remote no longer has' '
	rm -f err &&
	test_create_repo unreliable-server &&
	git -C unreliable-server config uploadpack.allowanysha1inwant 1 &&
	git -C unreliable-server config uploadpack.allowfilter 1 &&
	test_commit -C unreliable-server foo &&

	git clone --filter=blob:none --no-checkout "file://$(pwd)/unreliable-server" unreliable-client &&

	rm -rf unreliable-server/.git/objects/* &&
	test_must_fail git -C unreliable-client checkout HEAD 2>err &&
	grep "could not fetch.*from promisor remote" err
'

test_expect_success 'fetching of missing objects works with ref-in-want enabled' '
	# ref-in-want requires protocol version 2
	git -C server config protocol.version 2 &&
	git -C server config uploadpack.allowrefinwant 1 &&
	git -C repo config protocol.version 2 &&

	rm -rf repo/.git/objects/* &&
	rm -f trace &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C repo cat-file -p "$HASH" &&
	grep "fetch< fetch=.*ref-in-want" trace
'

test_expect_success 'fetching of missing objects from another promisor remote' '
	git clone "file://$(pwd)/server" server2 &&
	test_commit -C server2 bar &&
	git -C server2 repack -a -d --write-bitmap-index &&
	HASH2=$(git -C server2 rev-parse bar) &&

	git -C repo remote add server2 "file://$(pwd)/server2" &&
	git -C repo config remote.server2.promisor true &&
	git -C repo cat-file -p "$HASH2" &&

	git -C repo fetch server2 &&
	rm -rf repo/.git/objects/* &&
	git -C repo cat-file -p "$HASH2" &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH2" out
'

test_expect_success 'fetching of missing objects configures a promisor remote' '
	git clone "file://$(pwd)/server" server3 &&
	test_commit -C server3 baz &&
	git -C server3 repack -a -d --write-bitmap-index &&
	HASH3=$(git -C server3 rev-parse baz) &&
	git -C server3 config uploadpack.allowfilter 1 &&

	rm repo/.git/objects/pack/pack-*.promisor &&

	git -C repo remote add server3 "file://$(pwd)/server3" &&
	git -C repo fetch --filter="blob:none" server3 $HASH3 &&

	test_cmp_config -C repo true remote.server3.promisor &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.git/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH3" out
'

test_expect_success 'fetching of missing blobs works' '
	rm -rf server server2 repo &&
	rm -rf server server3 repo &&
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
	git -C repo rev-list --exclude-promisor-objects --objects bar >out &&
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
			--exclude-promisor-objects "$OBJ" || return 1
	done
'

test_expect_success 'single promisor remote can be re-initialized gracefully' '
	# ensure one promisor is in the promisors list
	rm -rf repo &&
	test_create_repo repo &&
	test_create_repo other &&
	git -C repo remote add foo "file://$(pwd)/other" &&
	git -C repo config remote.foo.promisor true &&
	git -C repo config extensions.partialclone foo &&

	# reinitialize the promisors list
	git -C repo fetch --filter=blob:none foo
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
	if test x"$1" = "x--must-fail"
	then
		shift
		test_must_fail git -C repo2 repack $1 -d
	else
		git -C repo2 repack $1 -d
	fi &&
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

	repack_and_check --must-fail -ab "$TWO" "$THREE" &&
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

test_expect_success 'do not fetch when checking existence of tree we construct ourselves' '
	rm -rf repo &&
	test_create_repo repo &&
	test_commit -C repo base &&
	test_commit -C repo side1 &&
	git -C repo checkout base &&
	test_commit -C repo side2 &&

	git -C repo config core.repositoryformatversion 1 &&
	git -C repo config extensions.partialclone "arbitrary string" &&

	git -C repo cherry-pick side1
'

test_expect_success 'exact rename does not need to fetch the blob lazily' '
	rm -rf repo partial.git &&
	test_create_repo repo &&
	content="some dummy content" &&
	test_commit -C repo create-a-file file.txt "$content" &&
	git -C repo mv file.txt new-file.txt &&
	git -C repo commit -m rename-the-file &&
	FILE_HASH=$(git -C repo rev-parse HEAD:new-file.txt) &&
	test_config -C repo uploadpack.allowfilter 1 &&
	test_config -C repo uploadpack.allowanysha1inwant 1 &&

	git clone --filter=blob:none --bare "file://$(pwd)/repo" partial.git &&
	git -C partial.git rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out &&
	git -C partial.git log --follow -- new-file.txt &&
	git -C partial.git rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out
'

test_expect_success 'lazy-fetch when accessing object not in the_repository' '
	rm -rf full partial.git &&
	test_create_repo full &&
	test_commit -C full create-a-file file.txt &&

	test_config -C full uploadpack.allowfilter 1 &&
	test_config -C full uploadpack.allowanysha1inwant 1 &&
	git clone --filter=blob:none --bare "file://$(pwd)/full" partial.git &&
	FILE_HASH=$(git -C full rev-parse HEAD:file.txt) &&

	# Sanity check that the file is missing
	git -C partial.git rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out &&

	# The no-lazy-fetch mechanism prevents Git from fetching
	test_must_fail env GIT_NO_LAZY_FETCH=1 \
		git -C partial.git cat-file -e "$FILE_HASH" &&

	# The same with command line option to "git"
	test_must_fail git --no-lazy-fetch -C partial.git cat-file -e "$FILE_HASH" &&

	# The same, forcing a subprocess via an alias
	test_must_fail git --no-lazy-fetch -C partial.git \
		-c alias.foo="!git cat-file" foo -e "$FILE_HASH" &&

	# Sanity check that the file is still missing
	git -C partial.git rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out &&

	git -C full cat-file -s "$FILE_HASH" >expect &&
	test-tool partial-clone object-info partial.git "$FILE_HASH" >actual &&
	test_cmp expect actual &&

	# Sanity check that the file is now present
	git -C partial.git rev-list --objects --missing=print HEAD >out &&
	! grep "[?]$FILE_HASH" out
'

test_expect_success 'push should not fetch new commit objects' '
	rm -rf server client &&
	test_create_repo server &&
	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	test_commit -C server server1 &&

	git clone --filter=blob:none "file://$(pwd)/server" client &&
	test_commit -C client client1 &&

	test_commit -C server server2 &&
	COMMIT=$(git -C server rev-parse server2) &&

	test_must_fail git -C client push 2>err &&
	grep "fetch first" err &&
	git -C client rev-list --objects --missing=print "$COMMIT" >objects &&
	grep "^[?]$COMMIT" objects
'

test_expect_success 'setup for promisor.quiet tests' '
	rm -rf server &&
	test_create_repo server &&
	test_commit -C server foo &&
	git -C server rm foo.t &&
	git -C server commit -m remove &&
	git -C server config uploadpack.allowanysha1inwant 1 &&
	git -C server config uploadpack.allowfilter 1
'

test_expect_success TTY 'promisor.quiet=false shows progress messages' '
	rm -rf repo &&
	git clone --filter=blob:none "file://$(pwd)/server" repo &&
	git -C repo config promisor.quiet "false" &&

	test_terminal git -C repo cat-file -p foo:foo.t 2>err &&

	# Ensure that progress messages are written
	grep "Receiving objects" err
'

test_expect_success TTY 'promisor.quiet=true does not show progress messages' '
	rm -rf repo &&
	git clone --filter=blob:none "file://$(pwd)/server" repo &&
	git -C repo config promisor.quiet "true" &&

	test_terminal git -C repo cat-file -p foo:foo.t 2>err &&

	# Ensure that no progress messages are written
	! grep "Receiving objects" err
'

test_expect_success TTY 'promisor.quiet=unconfigured shows progress messages' '
	rm -rf repo &&
	git clone --filter=blob:none "file://$(pwd)/server" repo &&

	test_terminal git -C repo cat-file -p foo:foo.t 2>err &&

	# Ensure that progress messages are written
	grep "Receiving objects" err
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
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	git verify-pack --verbose "$IDX" >out &&
	grep "$HASH" out
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
