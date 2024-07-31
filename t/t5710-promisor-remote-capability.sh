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
	test "$3" = "$(cat missing.txt)"
}

initialize_server () {
	# Repack everything first
	git -C server -c repack.writebitmaps=false repack -a -d &&

	# Remove promisor file in case they exist, useful when reinitializing
	rm -rf server/objects/pack/*.promisor &&

	# Repack without the largest object and create a promisor pack on server
	git -C server -c repack.writebitmaps=false repack -a -d \
	    --filter=blob:limit=5k --filter-to="$(pwd)" &&
	promisor_file=$(ls server/objects/pack/*.pack | sed "s/\.pack/.promisor/") &&
	touch "$promisor_file" &&

	# Check that only one object is missing on the server
	check_missing_objects server 1 "$oid"
}

test_expect_success "setup for testing promisor remote advertisement" '
	# Create another bare repo called "server2"
	git init --bare server2 &&

	# Copy the largest object from server to server2
	obj="HEAD:foo" &&
	oid="$(git -C server rev-parse $obj)" &&
	oid_path="$(test_oid_to_path $oid)" &&
	path="server/objects/$oid_path" &&
	path2="server2/objects/$oid_path" &&
	mkdir -p $(dirname "$path2") &&
	cp "$path" "$path2" &&

	initialize_server &&

	# Configure server2 as promisor remote for server
	git -C server remote add server2 "file://$(pwd)/server2" &&
	git -C server config remote.server2.promisor true &&

	git -C server2 config uploadpack.allowFilter true &&
	git -C server2 config uploadpack.allowAnySHA1InWant true &&
	git -C server config uploadpack.allowFilter true &&
	git -C server config uploadpack.allowAnySHA1InWant true
'

test_expect_success "fetch with promisor.advertise set to 'true'" '
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

test_expect_success "fetch with promisor.advertise set to 'false'" '
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
	initialize_server
'

test_expect_success "fetch with promisor.acceptfromserver set to 'None'" '
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
	initialize_server
'

test_expect_success "fetch with promisor.acceptfromserver set to 'KnownName'" '
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

test_expect_success "fetch with 'KnownName' and different remote names" '
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
	initialize_server
'

test_expect_success "fetch with promisor.acceptfromserver set to 'KnownUrl'" '
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

test_expect_success "fetch with 'KnownUrl' and different remote urls" '
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
	check_missing_objects server 0 ""
'

test_done
