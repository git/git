#!/bin/sh

test_description='git partial clone'

. ./test-lib.sh

# create a normal "src" repo where we can later create new commits.
# expect_1.oids will contain a list of the OIDs of all blobs.
test_expect_success 'setup normal src repo' '
	echo "{print \$1}" >print_1.awk &&
	echo "{print \$2}" >print_2.awk &&

	git init src &&
	for n in 1 2 3 4
	do
		echo "This is file: $n" > src/file.$n.txt
		git -C src add file.$n.txt
		git -C src commit -m "file $n"
		git -C src ls-files -s file.$n.txt >>temp
	done &&
	awk -f print_2.awk <temp | sort >expect_1.oids &&
	test_line_count = 4 expect_1.oids
'

# bare clone "src" giving "srv.bare" for use as our server.
test_expect_success 'setup bare clone for server' '
	git clone --bare "file://$(pwd)/src" srv.bare &&
	git -C srv.bare config --local uploadpack.allowfilter 1 &&
	git -C srv.bare config --local uploadpack.allowanysha1inwant 1
'

# do basic partial clone from "srv.bare"
# confirm we are missing all of the known blobs.
# confirm partial clone was registered in the local config.
test_expect_success 'do partial clone 1' '
	git clone --no-checkout --filter=blob:none "file://$(pwd)/srv.bare" pc1 &&

	git -C pc1 rev-list --quiet --objects --missing=print HEAD >revs &&
	awk -f print_1.awk revs |
	sed "s/?//" |
	sort >observed.oids &&

	test_cmp expect_1.oids observed.oids &&
	test "$(git -C pc1 config --local core.repositoryformatversion)" = "1" &&
	test "$(git -C pc1 config --local extensions.partialclone)" = "origin" &&
	test "$(git -C pc1 config --local core.partialclonefilter)" = "blob:none"
'

# checkout master to force dynamic object fetch of blobs at HEAD.
test_expect_success 'verify checkout with dynamic object fetch' '
	git -C pc1 rev-list --quiet --objects --missing=print HEAD >observed &&
	test_line_count = 4 observed &&
	git -C pc1 checkout master &&
	git -C pc1 rev-list --quiet --objects --missing=print HEAD >observed &&
	test_line_count = 0 observed
'

# create new commits in "src" repo to establish a blame history on file.1.txt
# and push to "srv.bare".
test_expect_success 'push new commits to server' '
	git -C src remote add srv "file://$(pwd)/srv.bare" &&
	for x in a b c d e
	do
		echo "Mod file.1.txt $x" >>src/file.1.txt
		git -C src add file.1.txt
		git -C src commit -m "mod $x"
	done &&
	git -C src blame master -- file.1.txt >expect.blame &&
	git -C src push -u srv master
'

# (partial) fetch in the partial clone repo from the promisor remote.
# verify that fetch inherited the filter-spec from the config and DOES NOT
# have the new blobs.
test_expect_success 'partial fetch inherits filter settings' '
	git -C pc1 fetch origin &&
	git -C pc1 rev-list --quiet --objects --missing=print \
		master..origin/master >observed &&
	test_line_count = 5 observed
'

# force dynamic object fetch using diff.
# we should only get 1 new blob (for the file in origin/master).
test_expect_success 'verify diff causes dynamic object fetch' '
	git -C pc1 diff master..origin/master -- file.1.txt &&
	git -C pc1 rev-list --quiet --objects --missing=print \
		 master..origin/master >observed &&
	test_line_count = 4 observed
'

# force full dynamic object fetch of the file's history using blame.
# we should get the intermediate blobs for the file.
test_expect_success 'verify blame causes dynamic object fetch' '
	git -C pc1 blame origin/master -- file.1.txt >observed.blame &&
	test_cmp expect.blame observed.blame &&
	git -C pc1 rev-list --quiet --objects --missing=print \
		master..origin/master >observed &&
	test_line_count = 0 observed
'

# create new commits in "src" repo to establish a history on file.2.txt
# and push to "srv.bare".
test_expect_success 'push new commits to server for file.2.txt' '
	for x in a b c d e f
	do
		echo "Mod file.2.txt $x" >>src/file.2.txt
		git -C src add file.2.txt
		git -C src commit -m "mod $x"
	done &&
	git -C src push -u srv master
'

# Do FULL fetch by disabling inherited filter-spec using --no-filter.
# Verify we have all the new blobs.
test_expect_success 'override inherited filter-spec using --no-filter' '
	git -C pc1 fetch --no-filter origin &&
	git -C pc1 rev-list --quiet --objects --missing=print \
		master..origin/master >observed &&
	test_line_count = 0 observed
'

# create new commits in "src" repo to establish a history on file.3.txt
# and push to "srv.bare".
test_expect_success 'push new commits to server for file.3.txt' '
	for x in a b c d e f
	do
		echo "Mod file.3.txt $x" >>src/file.3.txt
		git -C src add file.3.txt
		git -C src commit -m "mod $x"
	done &&
	git -C src push -u srv master
'

# Do a partial fetch and then try to manually fetch the missing objects.
# This can be used as the basis of a pre-command hook to bulk fetch objects
# perhaps combined with a command in dry-run mode.
test_expect_success 'manual prefetch of missing objects' '
	git -C pc1 fetch --filter=blob:none origin &&

	git -C pc1 rev-list --quiet --objects --missing=print \
		 master..origin/master >revs &&
	awk -f print_1.awk revs |
	sed "s/?//" |
	sort >observed.oids &&

	test_line_count = 6 observed.oids &&
	git -C pc1 fetch-pack --stdin "file://$(pwd)/srv.bare" <observed.oids &&

	git -C pc1 rev-list --quiet --objects --missing=print \
		master..origin/master >revs &&
	awk -f print_1.awk revs |
	sed "s/?//" |
	sort >observed.oids &&

	test_line_count = 0 observed.oids
'

test_expect_success 'partial clone with transfer.fsckobjects=1 uses index-pack --fsck-objects' '
	git init src &&
	test_commit -C src x &&
	test_config -C src uploadpack.allowfilter 1 &&
	test_config -C src uploadpack.allowanysha1inwant 1 &&

	GIT_TRACE="$(pwd)/trace" git -c transfer.fsckobjects=1 \
		clone --filter="blob:none" "file://$(pwd)/src" dst &&
	grep "git index-pack.*--fsck-objects" trace
'

test_expect_success 'use fsck before and after manually fetching a missing subtree' '
	# push new commit so server has a subtree
	mkdir src/dir &&
	echo "in dir" >src/dir/file.txt &&
	git -C src add dir/file.txt &&
	git -C src commit -m "file in dir" &&
	git -C src push -u srv master &&
	SUBTREE=$(git -C src rev-parse HEAD:dir) &&

	rm -rf dst &&
	git clone --no-checkout --filter=tree:0 "file://$(pwd)/srv.bare" dst &&
	git -C dst fsck &&

	# Make sure we only have commits, and all trees and blobs are missing.
	git -C dst rev-list --missing=allow-any --objects master \
		>fetched_objects &&
	awk -f print_1.awk fetched_objects |
	xargs -n1 git -C dst cat-file -t >fetched_types &&

	sort -u fetched_types >unique_types.observed &&
	echo commit >unique_types.expected &&
	test_cmp unique_types.expected unique_types.observed &&

	# Auto-fetch a tree with cat-file.
	git -C dst cat-file -p $SUBTREE >tree_contents &&
	grep file.txt tree_contents &&

	# fsck still works after an auto-fetch of a tree.
	git -C dst fsck &&

	# Auto-fetch all remaining trees and blobs with --missing=error
	git -C dst rev-list --missing=error --objects master >fetched_objects &&
	test_line_count = 70 fetched_objects &&

	awk -f print_1.awk fetched_objects |
	xargs -n1 git -C dst cat-file -t >fetched_types &&

	sort -u fetched_types >unique_types.observed &&
	test_write_lines blob commit tree >unique_types.expected &&
	test_cmp unique_types.expected unique_types.observed
'

test_expect_success 'partial clone fetches blobs pointed to by refs even if normally filtered out' '
	rm -rf src dst &&
	git init src &&
	test_commit -C src x &&
	test_config -C src uploadpack.allowfilter 1 &&
	test_config -C src uploadpack.allowanysha1inwant 1 &&

	# Create a tag pointing to a blob.
	BLOB=$(echo blob-contents | git -C src hash-object --stdin -w) &&
	git -C src tag myblob "$BLOB" &&

	git clone --filter="blob:none" "file://$(pwd)/src" dst 2>err &&
	! grep "does not point to a valid object" err &&
	git -C dst fsck
'

test_expect_success 'fetch what is specified on CLI even if already promised' '
	rm -rf src dst.git &&
	git init src &&
	test_commit -C src foo &&
	test_config -C src uploadpack.allowfilter 1 &&
	test_config -C src uploadpack.allowanysha1inwant 1 &&

	git hash-object --stdin <src/foo.t >blob &&

	git clone --bare --filter=blob:none "file://$(pwd)/src" dst.git &&
	git -C dst.git rev-list --objects --quiet --missing=print HEAD >missing_before &&
	grep "?$(cat blob)" missing_before &&
	git -C dst.git fetch origin $(cat blob) &&
	git -C dst.git rev-list --objects --quiet --missing=print HEAD >missing_after &&
	! grep "?$(cat blob)" missing_after
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

# Converts bytes into their hexadecimal representation. For example,
# "printf 'ab\r\n' | hex_unpack" results in '61620d0a'.
hex_unpack () {
	perl -e '$/ = undef; $input = <>; print unpack("H2" x length($input), $input)'
}

# Inserts $1 at the start of the string and every 2 characters thereafter.
intersperse () {
	sed 's/\(..\)/'$1'\1/g'
}

# Create a one-time-sed command to replace the existing packfile with $1.
replace_packfile () {
	# The protocol requires that the packfile be sent in sideband 1, hence
	# the extra \x01 byte at the beginning.
	printf "1,/packfile/!c %04x\\\\x01%s0000" \
		"$(($(wc -c <$1) + 5))" \
		"$(hex_unpack <$1 | intersperse '\\x')" \
		>"$HTTPD_ROOT_PATH/one-time-sed"
}

test_expect_success 'upon cloning, check that all refs point to objects' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	rm -rf "$SERVER" repo &&
	test_create_repo "$SERVER" &&
	test_commit -C "$SERVER" foo &&
	test_config -C "$SERVER" uploadpack.allowfilter 1 &&
	test_config -C "$SERVER" uploadpack.allowanysha1inwant 1 &&

	# Create a tag pointing to a blob.
	BLOB=$(echo blob-contents | git -C "$SERVER" hash-object --stdin -w) &&
	git -C "$SERVER" tag myblob "$BLOB" &&

	# Craft a packfile not including that blob.
	git -C "$SERVER" rev-parse HEAD |
	git -C "$SERVER" pack-objects --stdout >incomplete.pack &&

	# Replace the existing packfile with the crafted one. The protocol
	# requires that the packfile be sent in sideband 1, hence the extra
	# \x01 byte at the beginning.
	replace_packfile incomplete.pack &&

	# Use protocol v2 because the sed command looks for the "packfile"
	# section header.
	test_config -C "$SERVER" protocol.version 2 &&
	test_must_fail git -c protocol.version=2 clone \
		--filter=blob:none $HTTPD_URL/one_time_sed/server repo 2>err &&

	test_i18ngrep "did not send all necessary objects" err &&

	# Ensure that the one-time-sed script was used.
	! test -e "$HTTPD_ROOT_PATH/one-time-sed"
'

test_expect_success 'when partial cloning, tolerate server not sending target of tag' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	rm -rf "$SERVER" repo &&
	test_create_repo "$SERVER" &&
	test_commit -C "$SERVER" foo &&
	test_config -C "$SERVER" uploadpack.allowfilter 1 &&
	test_config -C "$SERVER" uploadpack.allowanysha1inwant 1 &&

	# Create an annotated tag pointing to a blob.
	BLOB=$(echo blob-contents | git -C "$SERVER" hash-object --stdin -w) &&
	git -C "$SERVER" tag -m message -a myblob "$BLOB" &&

	# Craft a packfile including the tag, but not the blob it points to.
	# Also, omit objects referenced from HEAD in order to force a second
	# fetch (to fetch missing objects) upon the automatic checkout that
	# happens after a clone.
	printf "%s\n%s\n--not\n%s\n%s\n" \
		$(git -C "$SERVER" rev-parse HEAD) \
		$(git -C "$SERVER" rev-parse myblob) \
		$(git -C "$SERVER" rev-parse HEAD^{tree}) \
		$(git -C "$SERVER" rev-parse myblob^{blob}) |
		git -C "$SERVER" pack-objects --thin --stdout >incomplete.pack &&

	# Replace the existing packfile with the crafted one. The protocol
	# requires that the packfile be sent in sideband 1, hence the extra
	# \x01 byte at the beginning.
	replace_packfile incomplete.pack &&

	# Use protocol v2 because the sed command looks for the "packfile"
	# section header.
	test_config -C "$SERVER" protocol.version 2 &&

	# Exercise to make sure it works.
	git -c protocol.version=2 clone \
		--filter=blob:none $HTTPD_URL/one_time_sed/server repo 2> err &&
	! grep "missing object referenced by" err &&

	# Ensure that the one-time-sed script was used.
	! test -e "$HTTPD_ROOT_PATH/one-time-sed"
'

test_expect_success 'tolerate server sending REF_DELTA against missing promisor objects' '
	SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server" &&
	rm -rf "$SERVER" repo &&
	test_create_repo "$SERVER" &&
	test_config -C "$SERVER" uploadpack.allowfilter 1 &&
	test_config -C "$SERVER" uploadpack.allowanysha1inwant 1 &&

	# Create a commit with 2 blobs to be used as delta bases.
	for i in $(test_seq 10)
	do
		echo "this is a line" >>"$SERVER/foo.txt" &&
		echo "this is another line" >>"$SERVER/have.txt"
	done &&
	git -C "$SERVER" add foo.txt have.txt &&
	git -C "$SERVER" commit -m bar &&
	git -C "$SERVER" rev-parse HEAD:foo.txt >deltabase_missing &&
	git -C "$SERVER" rev-parse HEAD:have.txt >deltabase_have &&

	# Clone. The client has deltabase_have but not deltabase_missing.
	git -c protocol.version=2 clone --no-checkout \
		--filter=blob:none $HTTPD_URL/one_time_sed/server repo &&
	git -C repo hash-object -w -- "$SERVER/have.txt" &&

	# Sanity check to ensure that the client does not have
	# deltabase_missing.
	git -C repo rev-list --objects --ignore-missing \
		-- $(cat deltabase_missing) >objlist &&
	test_line_count = 0 objlist &&

	# Another commit. This commit will be fetched by the client.
	echo "abcdefghijklmnopqrstuvwxyz" >>"$SERVER/foo.txt" &&
	echo "abcdefghijklmnopqrstuvwxyz" >>"$SERVER/have.txt" &&
	git -C "$SERVER" add foo.txt have.txt &&
	git -C "$SERVER" commit -m baz &&

	# Pack a thin pack containing, among other things, HEAD:foo.txt
	# delta-ed against HEAD^:foo.txt and HEAD:have.txt delta-ed against
	# HEAD^:have.txt.
	printf "%s\n--not\n%s\n" \
		$(git -C "$SERVER" rev-parse HEAD) \
		$(git -C "$SERVER" rev-parse HEAD^) |
		git -C "$SERVER" pack-objects --thin --stdout >thin.pack &&

	# Ensure that the pack contains one delta against HEAD^:foo.txt. Since
	# the delta contains at least 26 novel characters, the size cannot be
	# contained in 4 bits, so the object header will take up 2 bytes. The
	# most significant nybble of the first byte is 0b1111 (0b1 to indicate
	# that the header continues, and 0b111 to indicate REF_DELTA), followed
	# by any 3 nybbles, then the OID of the delta base.
	printf "f.,..%s" $(intersperse "," <deltabase_missing) >want &&
	hex_unpack <thin.pack | intersperse "," >have &&
	grep $(cat want) have &&

	# Ensure that the pack contains one delta against HEAD^:have.txt,
	# similar to the above.
	printf "f.,..%s" $(intersperse "," <deltabase_have) >want &&
	hex_unpack <thin.pack | intersperse "," >have &&
	grep $(cat want) have &&

	replace_packfile thin.pack &&

	# Use protocol v2 because the sed command looks for the "packfile"
	# section header.
	test_config -C "$SERVER" protocol.version 2 &&

	# Fetch the thin pack and ensure that index-pack is able to handle the
	# REF_DELTA object with a missing promisor delta base.
	GIT_TRACE_PACKET="$(pwd)/trace" git -C repo -c protocol.version=2 fetch &&

	# Ensure that the missing delta base was directly fetched, but not the
	# one that the client has.
	grep "want $(cat deltabase_missing)" trace &&
	! grep "want $(cat deltabase_have)" trace &&

	# Ensure that the one-time-sed script was used.
	! test -e "$HTTPD_ROOT_PATH/one-time-sed"
'

test_done
