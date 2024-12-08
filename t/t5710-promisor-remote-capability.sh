#!/bin/sh

test_description='handling of promisor remote advertisement'

. ./test-lib.sh

# Setup the repository with three commits, this way HEAD is always
# available and we can hide commit 1 or 2.
test_expect_success 'setup: create "template" repository' '
	git init template &&
	test_commit -C template 1 &&
	test_commit -C template 2 &&
	test_commit -C template 3 &&
	test-tool genrandom foo 10240 >template/foo &&
	git -C template add foo &&
	git -C template commit -m foo
'

# A bare repo will act as a server repo with unpacked objects.
test_expect_success 'setup: create bare "server" repository' '
	git clone --bare --no-local template server &&
	mv server/objects/pack/pack-* . &&
	packfile=$(ls pack-*.pack) &&
	git -C server unpack-objects --strict <"$packfile"
'

check_missing_objects () {
	git -C "$1" rev-list --objects --all --missing=print > all.txt &&
	perl -ne 'print if s/^[?]//' all.txt >missing.txt &&
	test_line_count = "$2" missing.txt &&
	if test "$2" -lt 2
	then
		test "$3" = "$(cat missing.txt)"
	else
		test -f "$3" &&
		sort <"$3" >expected_sorted &&
		sort <missing.txt >actual_sorted &&
		test_cmp expected_sorted actual_sorted
	fi
}

initialize_server () {
	count="$1"
	missing_oids="$2"

	# Repack everything first
	git -C server -c repack.writebitmaps=false repack -a -d &&

	# Remove promisor file in case they exist, useful when reinitializing
	rm -rf server/objects/pack/*.promisor &&

	# Repack without the largest object and create a promisor pack on server
	git -C server -c repack.writebitmaps=false repack -a -d \
	    --filter=blob:limit=5k --filter-to="$(pwd)/pack" &&
	promisor_file=$(ls server/objects/pack/*.pack | sed "s/\.pack/.promisor/") &&
	>"$promisor_file" &&

	# Check objects missing on the server
	check_missing_objects server "$count" "$missing_oids"
}

copy_to_server2 () {
	oid_path="$(test_oid_to_path $1)" &&
	path="server/objects/$oid_path" &&
	path2="server2/objects/$oid_path" &&
	mkdir -p $(dirname "$path2") &&
	cp "$path" "$path2"
}

test_expect_success "setup for testing promisor remote advertisement" '
	# Create another bare repo called "server2"
	git init --bare server2 &&

	# Copy the largest object from server to server2
	obj="HEAD:foo" &&
	oid="$(git -C server rev-parse $obj)" &&
	copy_to_server2 "$oid" &&

	initialize_server 1 "$oid" &&

	# Configure server2 as promisor remote for server
	git -C server remote add server2 "file://$(pwd)/server2" &&
	git -C server config remote.server2.promisor true &&

	git -C server2 config uploadpack.allowFilter true &&
	git -C server2 config uploadpack.allowAnySHA1InWant true &&
	git -C server config uploadpack.allowFilter true &&
	git -C server config uploadpack.allowAnySHA1InWant true
'

test_expect_success "clone with promisor.advertise set to 'true'" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.advertise set to 'false'" '
	git -C server config promisor.advertise false &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with promisor.acceptfromserver set to 'None'" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=None \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "init + fetch with promisor.advertise set to 'true'" '
	git -C server config promisor.advertise true &&

	test_when_finished "rm -rf client" &&
	mkdir client &&
	git -C client init &&
	git -C client config remote.server2.promisor true &&
	git -C client config remote.server2.fetch "+refs/heads/*:refs/remotes/server2/*" &&
	git -C client config remote.server2.url "file://$(pwd)/server2" &&
	git -C client config remote.server.url "file://$(pwd)/server" &&
	git -C client config remote.server.fetch "+refs/heads/*:refs/remotes/server/*" &&
	git -C client config promisor.acceptfromserver All &&
	GIT_NO_LAZY_FETCH=0 git -C client fetch --filter="blob:limit=5k" server &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.acceptfromserver set to 'KnownName'" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=KnownName \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'KnownName' and different remote names" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.serverTwo.promisor=true \
		-c remote.serverTwo.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.serverTwo.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=KnownName \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with promisor.acceptfromserver set to 'KnownUrl'" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=KnownUrl \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'KnownUrl' and different remote urls" '
	ln -s server2 serverTwo &&

	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/serverTwo" \
		-c promisor.acceptfromserver=KnownUrl \
		--no-local --filter="blob:limit=5k" server client &&
	test_when_finished "rm -rf client" &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with promisor.advertise set to 'true' but don't delete the client" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.server2.promisor=true \
		-c remote.server2.fetch="+refs/heads/*:refs/remotes/server2/*" \
		-c remote.server2.url="file://$(pwd)/server2" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "setup for subsequent fetches" '
	# Generate new commit with large blob
	test-tool genrandom bar 10240 >template/bar &&
	git -C template add bar &&
	git -C template commit -m bar &&

	# Fetch new commit with large blob
	git -C server fetch origin &&
	git -C server update-ref HEAD FETCH_HEAD &&
	git -C server rev-parse HEAD >expected_head &&

	# Repack everything twice and remove .promisor files before
	# each repack. This makes sure everything gets repacked
	# into a single packfile. The second repack is necessary
	# because the first one fetches from server2 and creates a new
	# packfile and its associated .promisor file.

	rm -f server/objects/pack/*.promisor &&
	git -C server -c repack.writebitmaps=false repack -a -d &&
	rm -f server/objects/pack/*.promisor &&
	git -C server -c repack.writebitmaps=false repack -a -d &&

	# Unpack everything
	rm pack-* &&
	mv server/objects/pack/pack-* . &&
	packfile=$(ls pack-*.pack) &&
	git -C server unpack-objects --strict <"$packfile" &&

	# Copy new large object to server2
	obj_bar="HEAD:bar" &&
	oid_bar="$(git -C server rev-parse $obj_bar)" &&
	copy_to_server2 "$oid_bar" &&

	# Reinitialize server so that the 2 largest objects are missing
	printf "%s\n" "$oid" "$oid_bar" >expected_missing.txt &&
	initialize_server 2 expected_missing.txt &&

	# Create one more client
	cp -r client client2
'

test_expect_success "subsequent fetch from a client when promisor.advertise is true" '
	git -C server config promisor.advertise true &&

	GIT_NO_LAZY_FETCH=0 git -C client pull origin &&

	git -C client rev-parse HEAD >actual &&
	test_cmp expected_head actual &&

	cat client/bar >/dev/null &&

	check_missing_objects server 2 expected_missing.txt
'

test_expect_success "subsequent fetch from a client when promisor.advertise is false" '
	git -C server config promisor.advertise false &&

	GIT_NO_LAZY_FETCH=0 git -C client2 pull origin &&

	git -C client2 rev-parse HEAD >actual &&
	test_cmp expected_head actual &&

	cat client2/bar >/dev/null &&

	check_missing_objects server 1 "$oid"
'

test_done
