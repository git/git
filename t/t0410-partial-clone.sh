#!/bin/sh

test_description='partial clone'

. ./test-lib.sh

# missing promisor objects cause repacks which write bitmaps to fail
GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0
# When enabled, some commands will write cummit-graphs. This causes fsck
# to fail when delete_object() is called because fsck will attempt to
# verify the out-of-sync cummit graph.
GIT_TEST_CUMMIT_GRAPH=0

delete_object () {
	rm $1/.but/objects/$(echo $2 | sed -e 's|^..|&/|')
}

pack_as_from_promisor () {
	HASH=$(but -C repo pack-objects .but/objects/pack/pack) &&
	>repo/.but/objects/pack/pack-$HASH.promisor &&
	echo $HASH
}

promise_and_delete () {
	HASH=$(but -C repo rev-parse "$1") &&
	but -C repo tag -a -m message my_annotated_tag "$HASH" &&
	but -C repo rev-parse my_annotated_tag | pack_as_from_promisor &&
	# tag -d prints a message to stdout, so redirect it
	but -C repo tag -d my_annotated_tag >/dev/null &&
	delete_object repo "$HASH"
}

test_expect_success 'extensions.partialclone without filter' '
	test_create_repo server &&
	but clone --filter="blob:none" "file://$(pwd)/server" client &&
	but -C client config --unset remote.origin.partialclonefilter &&
	but -C client fetch origin
'

test_expect_success 'convert shallow clone to partial clone' '
	rm -fr server client &&
	test_create_repo server &&
	test_cummit -C server my_cummit 1 &&
	test_cummit -C server my_cummit2 1 &&
	but clone --depth=1 "file://$(pwd)/server" client &&
	but -C client fetch --unshallow --filter="blob:none" &&
	test_cmp_config -C client true remote.origin.promisor &&
	test_cmp_config -C client blob:none remote.origin.partialclonefilter &&
	test_cmp_config -C client 1 core.repositoryformatversion
'

test_expect_success SHA1 'convert to partial clone with noop extension' '
	rm -fr server client &&
	test_create_repo server &&
	test_cummit -C server my_cummit 1 &&
	test_cummit -C server my_cummit2 1 &&
	but clone --depth=1 "file://$(pwd)/server" client &&
	test_cmp_config -C client 0 core.repositoryformatversion &&
	but -C client config extensions.noop true &&
	but -C client fetch --unshallow --filter="blob:none"
'

test_expect_success SHA1 'converting to partial clone fails with unrecognized extension' '
	rm -fr server client &&
	test_create_repo server &&
	test_cummit -C server my_cummit 1 &&
	test_cummit -C server my_cummit2 1 &&
	but clone --depth=1 "file://$(pwd)/server" client &&
	test_cmp_config -C client 0 core.repositoryformatversion &&
	but -C client config extensions.nonsense true &&
	test_must_fail but -C client fetch --unshallow --filter="blob:none"
'

test_expect_success 'missing reflog object, but promised by a cummit, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo my_cummit &&

	A=$(but -C repo cummit-tree -m a HEAD^{tree}) &&
	C=$(but -C repo cummit-tree -m c -p $A HEAD^{tree}) &&

	# Reference $A only from reflog, and delete it
	but -C repo branch my_branch "$A" &&
	but -C repo branch -f my_branch my_cummit &&
	delete_object repo "$A" &&

	# State that we got $C, which refers to $A, from promisor
	printf "$C\n" | pack_as_from_promisor &&

	# Normally, it fails
	test_must_fail but -C repo fsck &&

	# But with the extension, it succeeds
	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo fsck
'

test_expect_success 'missing reflog object, but promised by a tag, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo my_cummit &&

	A=$(but -C repo cummit-tree -m a HEAD^{tree}) &&
	but -C repo tag -a -m d my_tag_name $A &&
	T=$(but -C repo rev-parse my_tag_name) &&
	but -C repo tag -d my_tag_name &&

	# Reference $A only from reflog, and delete it
	but -C repo branch my_branch "$A" &&
	but -C repo branch -f my_branch my_cummit &&
	delete_object repo "$A" &&

	# State that we got $T, which refers to $A, from promisor
	printf "$T\n" | pack_as_from_promisor &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo fsck
'

test_expect_success 'missing reflog object alone fails fsck, even with extension set' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo my_cummit &&

	A=$(but -C repo cummit-tree -m a HEAD^{tree}) &&
	B=$(but -C repo cummit-tree -m b HEAD^{tree}) &&

	# Reference $A only from reflog, and delete it
	but -C repo branch my_branch "$A" &&
	but -C repo branch -f my_branch my_cummit &&
	delete_object repo "$A" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	test_must_fail but -C repo fsck
'

test_expect_success 'missing ref object, but promised, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo my_cummit &&

	A=$(but -C repo cummit-tree -m a HEAD^{tree}) &&

	# Reference $A only from ref
	but -C repo branch my_branch "$A" &&
	promise_and_delete "$A" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo fsck
'

test_expect_success 'missing object, but promised, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo 1 &&
	test_cummit -C repo 2 &&
	test_cummit -C repo 3 &&
	but -C repo tag -a annotated_tag -m "annotated tag" &&

	C=$(but -C repo rev-parse 1) &&
	T=$(but -C repo rev-parse 2^{tree}) &&
	B=$(but hash-object repo/3.t) &&
	AT=$(but -C repo rev-parse annotated_tag) &&

	promise_and_delete "$C" &&
	promise_and_delete "$T" &&
	promise_and_delete "$B" &&
	promise_and_delete "$AT" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo fsck
'

test_expect_success 'missing CLI object, but promised, passes fsck' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo my_cummit &&

	A=$(but -C repo cummit-tree -m a HEAD^{tree}) &&
	promise_and_delete "$A" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo fsck "$A"
'

test_expect_success 'fetching of missing objects' '
	rm -rf repo err &&
	test_create_repo server &&
	test_cummit -C server foo &&
	but -C server repack -a -d --write-bitmap-index &&

	but clone "file://$(pwd)/server" repo &&
	HASH=$(but -C repo rev-parse foo) &&
	rm -rf repo/.but/objects/* &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "origin" &&
	but -C repo cat-file -p "$HASH" 2>err &&

	# Ensure that no spurious FETCH_HEAD messages are written
	! grep FETCH_HEAD err &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.but/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	but verify-pack --verbose "$IDX" >out &&
	grep "$HASH" out
'

test_expect_success 'fetching of missing objects works with ref-in-want enabled' '
	# ref-in-want requires protocol version 2
	but -C server config protocol.version 2 &&
	but -C server config uploadpack.allowrefinwant 1 &&
	but -C repo config protocol.version 2 &&

	rm -rf repo/.but/objects/* &&
	rm -f trace &&
	GIT_TRACE_PACKET="$(pwd)/trace" but -C repo cat-file -p "$HASH" &&
	grep "fetch< fetch=.*ref-in-want" trace
'

test_expect_success 'fetching of missing objects from another promisor remote' '
	but clone "file://$(pwd)/server" server2 &&
	test_cummit -C server2 bar &&
	but -C server2 repack -a -d --write-bitmap-index &&
	HASH2=$(but -C server2 rev-parse bar) &&

	but -C repo remote add server2 "file://$(pwd)/server2" &&
	but -C repo config remote.server2.promisor true &&
	but -C repo cat-file -p "$HASH2" &&

	but -C repo fetch server2 &&
	rm -rf repo/.but/objects/* &&
	but -C repo cat-file -p "$HASH2" &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.but/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	but verify-pack --verbose "$IDX" >out &&
	grep "$HASH2" out
'

test_expect_success 'fetching of missing objects configures a promisor remote' '
	but clone "file://$(pwd)/server" server3 &&
	test_cummit -C server3 baz &&
	but -C server3 repack -a -d --write-bitmap-index &&
	HASH3=$(but -C server3 rev-parse baz) &&
	but -C server3 config uploadpack.allowfilter 1 &&

	rm repo/.but/objects/pack/pack-*.promisor &&

	but -C repo remote add server3 "file://$(pwd)/server3" &&
	but -C repo fetch --filter="blob:none" server3 $HASH3 &&

	test_cmp_config -C repo true remote.server3.promisor &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.but/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	but verify-pack --verbose "$IDX" >out &&
	grep "$HASH3" out
'

test_expect_success 'fetching of missing blobs works' '
	rm -rf server server2 repo &&
	rm -rf server server3 repo &&
	test_create_repo server &&
	test_cummit -C server foo &&
	but -C server repack -a -d --write-bitmap-index &&

	but clone "file://$(pwd)/server" repo &&
	but hash-object repo/foo.t >blobhash &&
	rm -rf repo/.but/objects/* &&

	but -C server config uploadpack.allowanysha1inwant 1 &&
	but -C server config uploadpack.allowfilter 1 &&
	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "origin" &&

	but -C repo cat-file -p $(cat blobhash)
'

test_expect_success 'fetching of missing trees does not fetch blobs' '
	rm -rf server repo &&
	test_create_repo server &&
	test_cummit -C server foo &&
	but -C server repack -a -d --write-bitmap-index &&

	but clone "file://$(pwd)/server" repo &&
	but -C repo rev-parse foo^{tree} >treehash &&
	but hash-object repo/foo.t >blobhash &&
	rm -rf repo/.but/objects/* &&

	but -C server config uploadpack.allowanysha1inwant 1 &&
	but -C server config uploadpack.allowfilter 1 &&
	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "origin" &&
	but -C repo cat-file -p $(cat treehash) &&

	# Ensure that the tree, but not the blob, is fetched
	but -C repo rev-list --objects --missing=print $(cat treehash) >objects &&
	grep "^$(cat treehash)" objects &&
	grep "^[?]$(cat blobhash)" objects
'

test_expect_success 'rev-list stops traversal at missing and promised cummit' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo foo &&
	test_cummit -C repo bar &&

	FOO=$(but -C repo rev-parse foo) &&
	promise_and_delete "$FOO" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo rev-list --exclude-promisor-objects --objects bar >out &&
	grep $(but -C repo rev-parse bar) out &&
	! grep $FOO out
'

test_expect_success 'missing tree objects with --missing=allow-promisor and --exclude-promisor-objects' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo foo &&
	test_cummit -C repo bar &&
	test_cummit -C repo baz &&

	promise_and_delete $(but -C repo rev-parse bar^{tree}) &&
	promise_and_delete $(but -C repo rev-parse foo^{tree}) &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&

	but -C repo rev-list --missing=allow-promisor --objects HEAD >objs 2>rev_list_err &&
	test_must_be_empty rev_list_err &&
	# 3 cummits, 3 blobs, and 1 tree
	test_line_count = 7 objs &&

	# Do the same for --exclude-promisor-objects, but with all trees gone.
	promise_and_delete $(but -C repo rev-parse baz^{tree}) &&
	but -C repo rev-list --exclude-promisor-objects --objects HEAD >objs 2>rev_list_err &&
	test_must_be_empty rev_list_err &&
	# 3 cummits, no blobs or trees
	test_line_count = 3 objs
'

test_expect_success 'missing non-root tree object and rev-list' '
	rm -rf repo &&
	test_create_repo repo &&
	mkdir repo/dir &&
	echo foo >repo/dir/foo &&
	but -C repo add dir/foo &&
	but -C repo cummit -m "cummit dir/foo" &&

	promise_and_delete $(but -C repo rev-parse HEAD:dir) &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&

	but -C repo rev-list --missing=allow-any --objects HEAD >objs 2>rev_list_err &&
	test_must_be_empty rev_list_err &&
	# 1 cummit and 1 tree
	test_line_count = 2 objs
'

test_expect_success 'rev-list stops traversal at missing and promised tree' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo foo &&
	mkdir repo/a_dir &&
	echo something >repo/a_dir/something &&
	but -C repo add a_dir/something &&
	but -C repo cummit -m bar &&

	# foo^{tree} (tree referenced from cummit)
	TREE=$(but -C repo rev-parse foo^{tree}) &&

	# a tree referenced by HEAD^{tree} (tree referenced from tree)
	TREE2=$(but -C repo ls-tree HEAD^{tree} | grep " tree " | head -1 | cut -b13-52) &&

	promise_and_delete "$TREE" &&
	promise_and_delete "$TREE2" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo rev-list --exclude-promisor-objects --objects HEAD >out &&
	grep $(but -C repo rev-parse foo) out &&
	! grep $TREE out &&
	grep $(but -C repo rev-parse HEAD) out &&
	! grep $TREE2 out
'

test_expect_success 'rev-list stops traversal at missing and promised blob' '
	rm -rf repo &&
	test_create_repo repo &&
	echo something >repo/something &&
	but -C repo add something &&
	but -C repo cummit -m foo &&

	BLOB=$(but -C repo hash-object -w something) &&
	promise_and_delete "$BLOB" &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo rev-list --exclude-promisor-objects --objects HEAD >out &&
	grep $(but -C repo rev-parse HEAD) out &&
	! grep $BLOB out
'

test_expect_success 'rev-list stops traversal at promisor cummit, tree, and blob' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo foo &&
	test_cummit -C repo bar &&
	test_cummit -C repo baz &&

	cummit=$(but -C repo rev-parse foo) &&
	TREE=$(but -C repo rev-parse bar^{tree}) &&
	BLOB=$(but hash-object repo/baz.t) &&
	printf "%s\n%s\n%s\n" $cummit $TREE $BLOB | pack_as_from_promisor &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo rev-list --exclude-promisor-objects --objects HEAD >out &&
	! grep $cummit out &&
	! grep $TREE out &&
	! grep $BLOB out &&
	grep $(but -C repo rev-parse bar) out  # sanity check that some walking was done
'

test_expect_success 'rev-list dies for missing objects on cmd line' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo foo &&
	test_cummit -C repo bar &&
	test_cummit -C repo baz &&

	cummit=$(but -C repo rev-parse foo) &&
	TREE=$(but -C repo rev-parse bar^{tree}) &&
	BLOB=$(but hash-object repo/baz.t) &&

	promise_and_delete $cummit &&
	promise_and_delete $TREE &&
	promise_and_delete $BLOB &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&

	for OBJ in "$cummit" "$TREE" "$BLOB"; do
		test_must_fail but -C repo rev-list --objects \
			--exclude-promisor-objects "$OBJ" &&
		test_must_fail but -C repo rev-list --objects-edge-aggressive \
			--exclude-promisor-objects "$OBJ" &&

		# Do not die or crash when --ignore-missing is passed.
		but -C repo rev-list --ignore-missing --objects \
			--exclude-promisor-objects "$OBJ" &&
		but -C repo rev-list --ignore-missing --objects-edge-aggressive \
			--exclude-promisor-objects "$OBJ" || return 1
	done
'

test_expect_success 'single promisor remote can be re-initialized gracefully' '
	# ensure one promisor is in the promisors list
	rm -rf repo &&
	test_create_repo repo &&
	test_create_repo other &&
	but -C repo remote add foo "file://$(pwd)/other" &&
	but -C repo config remote.foo.promisor true &&
	but -C repo config extensions.partialclone foo &&

	# reinitialize the promisors list
	but -C repo fetch --filter=blob:none foo
'

test_expect_success 'gc repacks promisor objects separately from non-promisor objects' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo one &&
	test_cummit -C repo two &&

	TREE_ONE=$(but -C repo rev-parse one^{tree}) &&
	printf "$TREE_ONE\n" | pack_as_from_promisor &&
	TREE_TWO=$(but -C repo rev-parse two^{tree}) &&
	printf "$TREE_TWO\n" | pack_as_from_promisor &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo gc &&

	# Ensure that exactly one promisor packfile exists, and that it
	# contains the trees but not the cummits
	ls repo/.but/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	PROMISOR_PACKFILE=$(sed "s/.promisor/.pack/" <promisorlist) &&
	but verify-pack $PROMISOR_PACKFILE -v >out &&
	grep "$TREE_ONE" out &&
	grep "$TREE_TWO" out &&
	! grep "$(but -C repo rev-parse one)" out &&
	! grep "$(but -C repo rev-parse two)" out &&

	# Remove the promisor packfile and associated files
	rm $(sed "s/.promisor//" <promisorlist).* &&

	# Ensure that the single other pack contains the cummits, but not the
	# trees
	ls repo/.but/objects/pack/pack-*.pack >packlist &&
	test_line_count = 1 packlist &&
	but verify-pack repo/.but/objects/pack/pack-*.pack -v >out &&
	grep "$(but -C repo rev-parse one)" out &&
	grep "$(but -C repo rev-parse two)" out &&
	! grep "$TREE_ONE" out &&
	! grep "$TREE_TWO" out
'

test_expect_success 'gc does not repack promisor objects if there are none' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo one &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo gc &&

	# Ensure that only one pack exists
	ls repo/.but/objects/pack/pack-*.pack >packlist &&
	test_line_count = 1 packlist
'

repack_and_check () {
	rm -rf repo2 &&
	cp -r repo repo2 &&
	if test x"$1" = "x--must-fail"
	then
		shift
		test_must_fail but -C repo2 repack $1 -d
	else
		but -C repo2 repack $1 -d
	fi &&
	but -C repo2 fsck &&

	but -C repo2 cat-file -e $2 &&
	but -C repo2 cat-file -e $3
}

test_expect_success 'repack -d does not irreversibly delete promisor objects' '
	rm -rf repo &&
	test_create_repo repo &&
	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&

	but -C repo cummit --allow-empty -m one &&
	but -C repo cummit --allow-empty -m two &&
	but -C repo cummit --allow-empty -m three &&
	but -C repo cummit --allow-empty -m four &&
	ONE=$(but -C repo rev-parse HEAD^^^) &&
	TWO=$(but -C repo rev-parse HEAD^^) &&
	THREE=$(but -C repo rev-parse HEAD^) &&

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
	test_cummit -C repo my_cummit &&

	TREE_HASH=$(but -C repo rev-parse HEAD^{tree}) &&
	HASH=$(promise_and_delete $TREE_HASH) &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&
	but -C repo gc &&

	# Ensure that the promisor packfile still exists, and remove it
	test -e repo/.but/objects/pack/pack-$HASH.pack &&
	rm repo/.but/objects/pack/pack-$HASH.* &&

	# Ensure that the single other pack contains the cummit, but not the tree
	ls repo/.but/objects/pack/pack-*.pack >packlist &&
	test_line_count = 1 packlist &&
	but verify-pack repo/.but/objects/pack/pack-*.pack -v >out &&
	grep "$(but -C repo rev-parse HEAD)" out &&
	! grep "$TREE_HASH" out
'

test_expect_success 'do not fetch when checking existence of tree we construct ourselves' '
	rm -rf repo &&
	test_create_repo repo &&
	test_cummit -C repo base &&
	test_cummit -C repo side1 &&
	but -C repo checkout base &&
	test_cummit -C repo side2 &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "arbitrary string" &&

	but -C repo cherry-pick side1
'

test_expect_success 'exact rename does not need to fetch the blob lazily' '
	rm -rf repo partial.but &&
	test_create_repo repo &&
	content="some dummy content" &&
	test_cummit -C repo create-a-file file.txt "$content" &&
	but -C repo mv file.txt new-file.txt &&
	but -C repo cummit -m rename-the-file &&
	FILE_HASH=$(but -C repo rev-parse HEAD:new-file.txt) &&
	test_config -C repo uploadpack.allowfilter 1 &&
	test_config -C repo uploadpack.allowanysha1inwant 1 &&

	but clone --filter=blob:none --bare "file://$(pwd)/repo" partial.but &&
	but -C partial.but rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out &&
	but -C partial.but log --follow -- new-file.txt &&
	but -C partial.but rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out
'

test_expect_success 'lazy-fetch when accessing object not in the_repository' '
	rm -rf full partial.but &&
	test_create_repo full &&
	test_cummit -C full create-a-file file.txt &&

	test_config -C full uploadpack.allowfilter 1 &&
	test_config -C full uploadpack.allowanysha1inwant 1 &&
	but clone --filter=blob:none --bare "file://$(pwd)/full" partial.but &&
	FILE_HASH=$(but -C full rev-parse HEAD:file.txt) &&

	# Sanity check that the file is missing
	but -C partial.but rev-list --objects --missing=print HEAD >out &&
	grep "[?]$FILE_HASH" out &&

	but -C full cat-file -s "$FILE_HASH" >expect &&
	test-tool partial-clone object-info partial.but "$FILE_HASH" >actual &&
	test_cmp expect actual &&

	# Sanity check that the file is now present
	but -C partial.but rev-list --objects --missing=print HEAD >out &&
	! grep "[?]$FILE_HASH" out
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'fetching of missing objects from an HTTP server' '
	rm -rf repo &&
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	test_create_repo "$SERVER" &&
	test_cummit -C "$SERVER" foo &&
	but -C "$SERVER" repack -a -d --write-bitmap-index &&

	but clone $HTTPD_URL/smart/server repo &&
	HASH=$(but -C repo rev-parse foo) &&
	rm -rf repo/.but/objects/* &&

	but -C repo config core.repositoryformatversion 1 &&
	but -C repo config extensions.partialclone "origin" &&
	but -C repo cat-file -p "$HASH" &&

	# Ensure that the .promisor file is written, and check that its
	# associated packfile contains the object
	ls repo/.but/objects/pack/pack-*.promisor >promisorlist &&
	test_line_count = 1 promisorlist &&
	IDX=$(sed "s/promisor$/idx/" promisorlist) &&
	but verify-pack --verbose "$IDX" >out &&
	grep "$HASH" out
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
