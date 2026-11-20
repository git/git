#!/bin/sh

test_description='handling of promisor remote advertisement over HTTP'

. ./test-lib.sh

if ! test_have_prereq PERL_TEST_HELPERS
then
	skip_all='skipping promisor remote capabilities tests; Perl not available'
	test_done
fi

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL=0

# The setup and helpers below are copied from t5710, except that the
# server repository is in the HTTP document root.

SERVER="$HTTPD_DOCUMENT_ROOT_PATH/server.git"

check_missing_objects () {
	git -C "$1" rev-list --objects --all --missing=print > all.txt &&
	perl -ne 'print if s/^[?]//' all.txt >missing.txt &&
	test_line_count = "$2" missing.txt &&
	test "$3" = "$(cat missing.txt)"
}

initialize_server () {
	# Repack everything first
	git -C "$SERVER" -c repack.writebitmaps=false repack -a -d &&

	# Repack without the largest object and create a promisor pack on server
	git -C "$SERVER" -c repack.writebitmaps=false repack -a -d \
	    --filter=blob:limit=5k --filter-to="$(pwd)/pack" &&
	promisor_file=$(ls "$SERVER"/objects/pack/*.pack | sed "s/\.pack/.promisor/") &&
	>"$promisor_file" &&

	# Check objects missing on the server
	check_missing_objects "$SERVER" 1 "$1"
}

copy_to_lop () {
	oid_path="$(test_oid_to_path $1)" &&
	path="$SERVER/objects/$oid_path" &&
	path2="lop/objects/$oid_path" &&
	mkdir -p $(dirname "$path2") &&
	cp "$path" "$path2"
}

# On Windows, `pwd` returns a path like 'D:/foo/bar'. Prepend '/' to turn
# it into '/D:/foo/bar', which is what git expects in file:// URLs on Windows.
# On Unix, the path already starts with '/', so this is a no-op.
pwd_path=$(pwd)
case "$pwd_path" in
[a-zA-Z]:*) pwd_path="/$pwd_path" ;;
esac

TRASH_DIRECTORY_URL="file://$pwd_path"

test_expect_success 'setup: create "template" repository' '
	git init template &&
	test_commit -C template 1 &&
	test_commit -C template 2 &&
	test_commit -C template 3 &&
	test-tool genrandom foo 10k >template/foo &&
	git -C template add foo &&
	git -C template commit -m foo
'

# A bare repo will act as a server repo with unpacked objects.
test_expect_success 'setup: create bare "server" repository' '
	git clone --bare --no-local template "$SERVER" &&
	mv "$SERVER"/objects/pack/pack-* . &&
	packfile=$(ls pack-*.pack) &&
	git -C "$SERVER" unpack-objects --strict <"$packfile"
'

test_expect_success "setup for testing promisor remote advertisement" '
	# Create another bare repo called "lop" (for Large Object Promisor)
	git init --bare lop &&

	# Copy the largest object from server to lop
	obj="HEAD:foo" &&
	oid="$(git -C "$SERVER" rev-parse $obj)" &&
	copy_to_lop "$oid" &&

	initialize_server "$oid" &&

	# Configure lop as promisor remote for server
	git -C "$SERVER" remote add lop "$TRASH_DIRECTORY_URL/lop" &&
	git -C "$SERVER" config remote.lop.promisor true &&

	git -C lop config uploadpack.allowFilter true &&
	git -C lop config uploadpack.allowAnySHA1InWant true &&
	git -C "$SERVER" config uploadpack.allowFilter true &&
	git -C "$SERVER" config uploadpack.allowAnySHA1InWant true &&
	git -C "$SERVER" config promisor.advertise true
'

# Over HTTP, each request is handled by a separate upload-pack process,
# so the server knows that the client accepted "lop" only if the reply
# is sent with the "fetch" request itself.
test_expect_success "clone over HTTP with an accepted promisor remote" '
	test_when_finished "rm -rf client trace" &&

	GIT_TRACE_PACKET="$(pwd)/trace" git -c protocol.version=2 clone \
		-c remote.lop.promisor=true \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--filter="blob:limit=5k" "$HTTPD_URL/smart/server.git" client &&

	# Check that the reply is part of the fetch request
	sed -n "/clone> command=fetch/,/clone> 0001/p" trace >request &&
	test_grep "clone> promisor-remote=lop" request &&

	# Check that the largest object was lazily fetched from lop
	test_cmp template/foo client/foo &&

	# Check that the largest object is still missing on the server
	check_missing_objects "$SERVER" 1 "$oid"
'

test_done
