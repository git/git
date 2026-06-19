#!/bin/sh

test_description='handling of promisor remote advertisement'

. ./test-lib.sh

if ! test_have_prereq PERL_TEST_HELPERS
then
	skip_all='skipping promisor remote capabilities tests; Perl not available'
	test_done
fi

GIT_TEST_MULTI_PACK_INDEX=0
GIT_TEST_MULTI_PACK_INDEX_WRITE_INCREMENTAL=0

# Setup the repository with three commits, this way HEAD is always
# available and we can hide commit 1 or 2.
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

copy_to_lop () {
	oid_path="$(test_oid_to_path $1)" &&
	path="server/objects/$oid_path" &&
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

# Allowed characters: alphanumeric, standard path/URI (_ . ~ / : -),
# and those percent-encoded below (% space = , ;)
rest=$(printf "%s" "$pwd_path" | tr -d 'a-zA-Z0-9_.~/:% =,;-')
if test -n "$rest"
then
	skip_all="PWD contains unsupported special characters"
	test_done
fi

TRASH_DIRECTORY_URL="file://$pwd_path"

encoded_path=$(printf "%s" "$pwd_path" |
	       sed -e 's/%/%25/g' -e 's/ /%20/g' -e 's/=/%3D/g' \
		   -e 's/;/%3B/g' -e 's/,/%2C/g')

ENCODED_TRASH_DIRECTORY_URL="file://$encoded_path"

test_expect_success "setup for testing promisor remote advertisement" '
	# Create another bare repo called "lop" (for Large Object Promisor)
	git init --bare lop &&

	# Copy the largest object from server to lop
	obj="HEAD:foo" &&
	oid="$(git -C server rev-parse $obj)" &&
	copy_to_lop "$oid" &&

	initialize_server 1 "$oid" &&

	# Configure lop as promisor remote for server
	git -C server remote add lop "$TRASH_DIRECTORY_URL/lop" &&
	git -C server config remote.lop.promisor true &&

	git -C lop config uploadpack.allowFilter true &&
	git -C lop config uploadpack.allowAnySHA1InWant true &&
	git -C server config uploadpack.allowFilter true &&
	git -C server config uploadpack.allowAnySHA1InWant true
'

test_expect_success "clone with promisor.advertise set to 'true'" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.advertise set to 'false'" '
	git -C server config promisor.advertise false &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with promisor.acceptfromserver set to 'None'" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=None \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "init + fetch with promisor.advertise set to 'true'" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	git init client &&
	git -C client config remote.lop.promisor true &&
	git -C client config remote.lop.fetch "+refs/heads/*:refs/remotes/lop/*" &&
	git -C client config remote.lop.url "$TRASH_DIRECTORY_URL/lop" &&
	git -C client config remote.server.url "$TRASH_DIRECTORY_URL/server" &&
	git -C client config remote.server.fetch "+refs/heads/*:refs/remotes/server/*" &&
	git -C client config promisor.acceptfromserver All &&
	GIT_NO_LAZY_FETCH=0 git -C client fetch --filter="blob:limit=5k" server &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with two promisors but only one advertised" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client unused_lop" &&

	# Create a promisor that will be configured but not be used
	git init --bare unused_lop &&

	# Clone from server to create a client
	GIT_TRACE="$(pwd)/trace" GIT_NO_LAZY_FETCH=0 git clone \
		-c remote.unused_lop.promisor=true \
		-c remote.unused_lop.fetch="+refs/heads/*:refs/remotes/unused_lop/*" \
		-c remote.unused_lop.url="$TRASH_DIRECTORY_URL/unused_lop" \
		-c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that "unused_lop" appears before "lop" in the config
	printf "remote.%s.promisor true\n" "unused_lop" "lop" "origin" >expect &&
	git -C client config get --all --show-names --regexp "^remote\..*\.promisor$" >actual &&
	test_cmp expect actual &&

	# Check that "lop" was tried
	test_grep " fetch lop " trace &&
	# Check that "unused_lop" was not contacted
	# This means "lop", the accepted promisor, was tried first
	test_grep ! " fetch unused_lop " trace &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "init + fetch two promisors but only one advertised" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client unused_lop" &&

	# Create a promisor that will be configured but not be used
	git init --bare unused_lop &&

	git init client &&
	git -C client config remote.unused_lop.promisor true &&
	git -C client config remote.unused_lop.fetch "+refs/heads/*:refs/remotes/unused_lop/*" &&
	git -C client config remote.unused_lop.url "$TRASH_DIRECTORY_URL/unused_lop" &&
	git -C client config remote.lop.promisor true &&
	git -C client config remote.lop.fetch "+refs/heads/*:refs/remotes/lop/*" &&
	git -C client config remote.lop.url "$TRASH_DIRECTORY_URL/lop" &&
	git -C client config remote.server.url "$TRASH_DIRECTORY_URL/server" &&
	git -C client config remote.server.fetch "+refs/heads/*:refs/remotes/server/*" &&
	git -C client config promisor.acceptfromserver All &&

	# Check that "unused_lop" appears before "lop" in the config
	printf "remote.%s.promisor true\n" "unused_lop" "lop" >expect &&
	git -C client config get --all --show-names --regexp "^remote\..*\.promisor$" >actual &&
	test_cmp expect actual &&

	GIT_TRACE="$(pwd)/trace" GIT_NO_LAZY_FETCH=0 git -C client fetch --filter="blob:limit=5k" server &&

	# Check that "lop" was tried
	test_grep " fetch lop " trace &&
	# Check that "unused_lop" was not contacted
	# This means "lop", the accepted promisor, was tried first
	test_grep ! " fetch unused_lop " trace &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.acceptfromserver set to 'KnownName'" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=KnownName \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'KnownName' and different remote names" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.serverTwo.promisor=true \
		-c remote.serverTwo.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.serverTwo.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=KnownName \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with 'KnownName' and missing URL in the config" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	# Lazy fetching by the client from the LOP will fail because of the
	# missing URL in the client config, so the server will have to lazy
	# fetch from the LOP.
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c promisor.acceptfromserver=KnownName \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with promisor.acceptfromserver set to 'KnownUrl'" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=KnownUrl \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'KnownUrl' and different remote urls" '
	ln -s lop serverTwo &&

	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/serverTwo" \
		-c promisor.acceptfromserver=KnownUrl \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with 'KnownUrl' and url not configured on the server" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	test_when_finished "git -C server config set remote.lop.url \"$TRASH_DIRECTORY_URL/lop\"" &&
	git -C server config unset remote.lop.url &&

	# Clone from server to create a client
	# It should fail because the client will reject the LOP as URLs are
	# different, and the server cannot lazy fetch as the LOP URL is
	# missing, so the remote name will be used instead which will fail.
	test_must_fail env GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=KnownUrl \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'KnownUrl' and empty url, so not advertised" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	test_when_finished "git -C server config set remote.lop.url \"$TRASH_DIRECTORY_URL/lop\"" &&
	git -C server config set remote.lop.url "" &&

	# Clone from server to create a client
	# It should fail because the client will reject the LOP as an empty URL is
	# not advertised, and the server cannot lazy fetch as the LOP URL is empty,
	# so the remote name will be used instead which will fail.
	test_must_fail env GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=KnownUrl \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'None' but URL allowlisted" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'None' but URL not in allowlist" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="https://example.com/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with 'None' but URL allowlisted in one pattern out of two" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="https://example.com/*" \
		-c promisor.acceptFromServerUrl="$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with 'None', URL allowlisted, but client has different URL" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# The client configures "lop" with a different URL (serverTwo) than
	# what the server advertises (lop). Even though the advertised URL
	# matches the allowlist, the remote is rejected because the
	# configured URL does not match the advertised one.
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/serverTwo" \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with URL allowlisted and no remote already configured" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&
	test_when_finished "rm -f full_names" &&

	GIT_NO_LAZY_FETCH=0 git clone \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that exactly one remote has been auto-created, identified
	# by "remote.<name>.advertisedAs" == "lop".
	git -C client config get --all --show-names --regexp \
		"remote\..*\.advertisedas" >full_names &&
	test_line_count = 1 full_names &&
	REMOTE_NAME=$(sed "s/^remote\.\(.*\)\.advertisedas .*$/\1/" full_names) &&

	# Check ".url" and ".promisor" values
	printf "%s\n" "$TRASH_DIRECTORY_URL/lop" "true" >expect &&
	git -C client config "remote.$REMOTE_NAME.url" >actual &&
	git -C client config "remote.$REMOTE_NAME.promisor" >>actual &&
	test_cmp expect actual &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with named URL allowlisted and no pre-configured remote" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	GIT_NO_LAZY_FETCH=0 git clone \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="cdn=$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that a remote has been auto-created with the right "cdn" name and fields.
	printf "%s\n" "$TRASH_DIRECTORY_URL/lop" "true" "lop" >expect &&
	git -C client config "remote.cdn.url" >actual &&
	git -C client config "remote.cdn.promisor" >>actual &&
	git -C client config "remote.cdn.advertisedAs" >>actual &&
	test_cmp expect actual &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with URL allowlisted but colliding name" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	GIT_NO_LAZY_FETCH=0 git clone -c remote.cdn.promisor=true \
		-c remote.cdn.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.cdn.url="https://example.com/cdn" \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="cdn=$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that a remote has been auto-created with the right "cdn-1" name and fields.
	printf "%s\n" "$TRASH_DIRECTORY_URL/lop" "true" "lop" >expect &&
	git -C client config "remote.cdn-1.url" >actual &&
	git -C client config "remote.cdn-1.promisor" >>actual &&
	git -C client config "remote.cdn-1.advertisedAs" >>actual &&
	test_cmp expect actual &&

	# Check that the original "cdn" remote was not overwritten.
	printf "%s\n" "https://example.com/cdn" "true" >expect &&
	git -C client config "remote.cdn.url" >actual &&
	git -C client config "remote.cdn.promisor" >>actual &&
	test_cmp expect actual &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with URL allowlisted and reusable remote" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	GIT_NO_LAZY_FETCH=0 git clone \
		-c remote.cdn.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.cdn.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=None \
		-c promisor.acceptFromServerUrl="cdn=$ENCODED_TRASH_DIRECTORY_URL/*" \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the existing "cdn" remote has been properly updated.
	printf "%s\n" "$TRASH_DIRECTORY_URL/lop" "true" "lop" "+refs/heads/*:refs/remotes/lop/*" >expect &&
	git -C client config "remote.cdn.url" >actual &&
	git -C client config "remote.cdn.promisor" >>actual &&
	git -C client config "remote.cdn.advertisedAs" >>actual &&
	git -C client config "remote.cdn.fetch" >>actual &&
	test_cmp expect actual &&

	# Check that no new "cdn-1" remote has been created.
	test_must_fail git -C client config "remote.cdn-1.url" &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with invalid promisor.acceptFromServerUrl" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	# As "bad name" contains a space, which is not a valid remote name,
	# the pattern should be rejected with a warning and no remote created.
	GIT_NO_LAZY_FETCH=0 git clone \
		-c promisor.acceptfromserver=None \
		-c "promisor.acceptFromServerUrl=bad name=https://example.com/*" \
		--no-local --filter="blob:limit=5k" server client 2>err &&

	# Check that a warning was emitted
	test_grep "invalid remote name '\''bad name'\''" err &&

	# Check that no remote was auto-created
	test_must_fail git -C client config get --regexp "remote\..*\.advertisedas" &&

	# Check that the largest object is not missing on the server
	check_missing_objects server 0 "" &&

	# Reinitialize server so that the largest object is missing again
	initialize_server 1 "$oid"
'

test_expect_success "clone with promisor.sendFields" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	git -C server remote add otherLop "https://invalid.invalid"  &&
	git -C server config remote.otherLop.token "fooBar" &&
	git -C server config remote.otherLop.stuff "baz" &&
	git -C server config remote.otherLop.partialCloneFilter "blob:limit=10k" &&
	test_when_finished "git -C server remote remove otherLop" &&
	test_config -C server promisor.sendFields "partialCloneFilter, token" &&
	test_when_finished "rm trace" &&

	# Clone from server to create a client
	GIT_TRACE_PACKET="$(pwd)/trace" GIT_NO_LAZY_FETCH=0 git clone \
		-c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that fields are properly transmitted
	PR1="name=lop,url=$ENCODED_TRASH_DIRECTORY_URL/lop,partialCloneFilter=blob:none" &&
	PR2="name=otherLop,url=https://invalid.invalid,partialCloneFilter=blob:limit=10k,token=fooBar" &&
	test_grep "clone< promisor-remote=$PR1;$PR2" trace &&
	test_grep "clone> promisor-remote=lop;otherLop" trace &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.checkFields" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	git -C server remote add otherLop "https://invalid.invalid"  &&
	git -C server config remote.otherLop.token "fooBar" &&
	git -C server config remote.otherLop.stuff "baz" &&
	git -C server config remote.otherLop.partialCloneFilter "blob:limit=10k" &&
	test_when_finished "git -C server remote remove otherLop" &&
	test_config -C server promisor.sendFields "partialCloneFilter, token" &&
	test_when_finished "rm trace" &&

	# Clone from server to create a client
	GIT_TRACE_PACKET="$(pwd)/trace" GIT_NO_LAZY_FETCH=0 git clone \
		-c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c remote.lop.partialCloneFilter="blob:none" \
		-c promisor.acceptfromserver=All \
		-c promisor.checkFields=partialcloneFilter \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that fields are properly transmitted
	PR1="name=lop,url=$ENCODED_TRASH_DIRECTORY_URL/lop,partialCloneFilter=blob:none" &&
	PR2="name=otherLop,url=https://invalid.invalid,partialCloneFilter=blob:limit=10k,token=fooBar" &&
	test_grep "clone< promisor-remote=$PR1;$PR2" trace &&
	test_grep "clone> promisor-remote=lop" trace &&
	test_grep ! "clone> promisor-remote=lop;otherLop" trace &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.storeFields=partialCloneFilter" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client" &&

	git -C server remote add otherLop "https://invalid.invalid"  &&
	git -C server config remote.otherLop.token "fooBar" &&
	git -C server config remote.otherLop.stuff "baz" &&
	git -C server config remote.otherLop.partialCloneFilter "blob:limit=10k" &&
	test_when_finished "git -C server remote remove otherLop" &&

	git -C server config remote.lop.token "fooXXX" &&
	git -C server config remote.lop.partialCloneFilter "blob:limit=8k" &&

	test_config -C server promisor.sendFields "partialCloneFilter, token" &&
	test_when_finished "rm trace" &&

	# Clone from server to create a client
	GIT_TRACE_PACKET="$(pwd)/trace" GIT_NO_LAZY_FETCH=0 git clone \
		-c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c remote.lop.token="fooYYY" \
		-c remote.lop.partialCloneFilter="blob:none" \
		-c promisor.acceptfromserver=All \
		-c promisor.storeFields=partialcloneFilter \
		--no-local --filter="blob:limit=5k" server client 2>err &&

	# Check that the filter from the server is stored
	echo "blob:limit=8k" >expected &&
	git -C client config remote.lop.partialCloneFilter >actual &&
	test_cmp expected actual &&

	# Check that user is notified when the filter is stored
	test_grep "Storing new filter from server for remote '\''lop'\''" err &&
	test_grep "'\''blob:none'\'' -> '\''blob:limit=8k'\''" err &&

	# Check that the token from the server is NOT stored
	echo "fooYYY" >expected &&
	git -C client config remote.lop.token >actual &&
	test_cmp expected actual &&
	test_grep ! "Storing new token from server" err &&

	# Check that the filter for an unknown remote is NOT stored
	test_must_fail git -C client config remote.otherLop.partialCloneFilter >actual &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid" &&

	# Change the configuration on the server and fetch from the client
	git -C server config remote.lop.partialCloneFilter "blob:limit=7k" &&
	GIT_NO_LAZY_FETCH=0 git -C client fetch \
		--filter="blob:limit=5k" ../server 2>err &&

	# Check that the fetch updated the configuration on the client
	echo "blob:limit=7k" >expected &&
	git -C client config remote.lop.partialCloneFilter >actual &&
	test_cmp expected actual &&

	# Check that user is notified when the new filter is stored
	test_grep "Storing new filter from server for remote '\''lop'\''" err &&
	test_grep "'\''blob:limit=8k'\'' -> '\''blob:limit=7k'\''" err
'

test_expect_success "clone and fetch with --filter=auto" '
	git -C server config promisor.advertise true &&
	test_when_finished "rm -rf client trace" &&

	git -C server config remote.lop.partialCloneFilter "blob:limit=9500" &&
	test_config -C server promisor.sendFields "partialCloneFilter" &&

	GIT_TRACE_PACKET="$(pwd)/trace" GIT_NO_LAZY_FETCH=0 git clone \
		-c remote.lop.promisor=true \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--no-local --filter=auto server client 2>err &&

	test_grep "filter blob:limit=9500" trace &&
	test_grep ! "filter auto" trace &&

	# Verify "auto" is persisted in config
	echo auto >expected &&
	git -C client config remote.origin.partialCloneFilter >actual &&
	test_cmp expected actual &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid" &&

	# Now change the filter on the server
	git -C server config remote.lop.partialCloneFilter "blob:limit=5678" &&

	# Get a new commit on the server to ensure "git fetch" actually runs fetch-pack
	test_commit -C template new-commit &&
	git -C template push --all "$(pwd)/server" &&

	# Perform a fetch WITH --filter=auto
	rm -rf trace &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch --filter=auto &&

	# Verify that the new filter was used
	test_grep "filter blob:limit=5678" trace &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid" &&

	# Change the filter on the server again
	git -C server config remote.lop.partialCloneFilter "blob:limit=5432" &&

	# Get yet a new commit on the server to ensure fetch-pack runs
	test_commit -C template yet-a-new-commit &&
	git -C template push --all "$(pwd)/server" &&

	# Perform a fetch WITHOUT --filter=auto
	# Relies on "auto" being persisted in the client config
	rm -rf trace &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch &&

	# Verify that the new filter was used
	test_grep "filter blob:limit=5432" trace &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "clone with promisor.advertise set to 'true' but don't delete the client" '
	git -C server config promisor.advertise true &&

	# Clone from server to create a client
	GIT_NO_LAZY_FETCH=0 git clone -c remote.lop.promisor=true \
		-c remote.lop.fetch="+refs/heads/*:refs/remotes/lop/*" \
		-c remote.lop.url="$TRASH_DIRECTORY_URL/lop" \
		-c promisor.acceptfromserver=All \
		--no-local --filter="blob:limit=5k" server client &&

	# Check that the largest object is still missing on the server
	check_missing_objects server 1 "$oid"
'

test_expect_success "setup for subsequent fetches" '
	# Generate new commit with large blob
	test-tool genrandom bar 10k >template/bar &&
	git -C template add bar &&
	git -C template commit -m bar &&

	# Fetch new commit with large blob
	git -C server fetch origin &&
	git -C server update-ref HEAD FETCH_HEAD &&
	git -C server rev-parse HEAD >expected_head &&

	# Repack everything twice and remove .promisor files before
	# each repack. This makes sure everything gets repacked
	# into a single packfile. The second repack is necessary
	# because the first one fetches from lop and creates a new
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

	# Copy new large object to lop
	obj_bar="HEAD:bar" &&
	oid_bar="$(git -C server rev-parse $obj_bar)" &&
	copy_to_lop "$oid_bar" &&

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
