# Set up and run tests of the 'bundle-uri' command in protocol v2
#
# The test that includes this script should set BUNDLE_URI_PROTOCOL
# to one of "file", "git", or "http".

BUNDLE_URI_TEST_PARENT=
BUNDLE_URI_TEST_URI=
BUNDLE_URI_TEST_BUNDLE_URI=
case "$BUNDLE_URI_PROTOCOL" in
file)
	BUNDLE_URI_PARENT=file_parent
	BUNDLE_URI_REPO_URI="file://$PWD/file_parent"
	BUNDLE_URI_BUNDLE_URI="$BUNDLE_URI_REPO_URI/fake.bdl"
	test_set_prereq BUNDLE_URI_FILE
	;;
git)
	. "$TEST_DIRECTORY"/lib-git-daemon.sh
	start_git_daemon --export-all --enable=receive-pack
	BUNDLE_URI_PARENT="$GIT_DAEMON_DOCUMENT_ROOT_PATH/parent"
	BUNDLE_URI_REPO_URI="$GIT_DAEMON_URL/parent"
	BUNDLE_URI_BUNDLE_URI="$BUNDLE_URI_REPO_URI/fake.bdl"
	test_set_prereq BUNDLE_URI_GIT
	;;
http)
	. "$TEST_DIRECTORY"/lib-httpd.sh
	start_httpd
	BUNDLE_URI_PARENT="$HTTPD_DOCUMENT_ROOT_PATH/http_parent"
	BUNDLE_URI_REPO_URI="$HTTPD_URL/smart/http_parent"
	BUNDLE_URI_BUNDLE_URI="$BUNDLE_URI_REPO_URL/fake.bdl"
	test_set_prereq BUNDLE_URI_HTTP
	;;
*)
	BUG "Need to pass valid BUNDLE_URI_PROTOCOL (was \"$BUNDLE_URI_PROTOCOL\")"
	;;
esac

test_expect_success "setup protocol v2 $BUNDLE_URI_PROTOCOL:// tests" '
	git init "$BUNDLE_URI_PARENT" &&
	test_commit -C "$BUNDLE_URI_PARENT" one &&
	git -C "$BUNDLE_URI_PARENT" config uploadpack.advertiseBundleURIs true
'

case "$BUNDLE_URI_PROTOCOL" in
http)
	test_expect_success "setup config for $BUNDLE_URI_PROTOCOL:// tests" '
		git -C "$BUNDLE_URI_PARENT" config http.receivepack true
	'
	;;
*)
	;;
esac
BUNDLE_URI_BUNDLE_URI_ESCAPED=$(echo "$BUNDLE_URI_BUNDLE_URI" | test_uri_escape)

test_expect_success "connect with $BUNDLE_URI_PROTOCOL:// using protocol v2: no bundle-uri" '
	test_when_finished "rm -f log" &&
	test_when_finished "git -C \"$BUNDLE_URI_PARENT\" config uploadpack.advertiseBundleURIs true" &&
	git -C "$BUNDLE_URI_PARENT" config uploadpack.advertiseBundleURIs false &&

	GIT_TRACE_PACKET="$PWD/log" \
	git \
		-c protocol.version=2 \
		ls-remote --symref "$BUNDLE_URI_REPO_URI" \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	! grep bundle-uri log
'

test_expect_success "connect with $BUNDLE_URI_PROTOCOL:// using protocol v2: have bundle-uri" '
	test_when_finished "rm -f log" &&

	GIT_TRACE_PACKET="$PWD/log" \
	git \
		-c protocol.version=2 \
		ls-remote --symref "$BUNDLE_URI_REPO_URI" \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	# Server advertised bundle-uri capability
	grep "< bundle-uri" log
'

test_expect_success "clone with $BUNDLE_URI_PROTOCOL:// using protocol v2: request bundle-uris" '
	test_when_finished "rm -rf log* cloned*" &&

	GIT_TRACE_PACKET="$PWD/log" \
	git \
		-c transfer.bundleURI=false \
		-c protocol.version=2 \
		clone "$BUNDLE_URI_REPO_URI" cloned \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	# Server advertised bundle-uri capability
	grep "< bundle-uri" log &&

	# Client did not issue bundle-uri command
	! grep "> command=bundle-uri" log &&

	GIT_TRACE_PACKET="$PWD/log" \
	git \
		-c transfer.bundleURI=true \
		-c protocol.version=2 \
		clone "$BUNDLE_URI_REPO_URI" cloned2 \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log &&

	# Server advertised bundle-uri capability
	grep "< bundle-uri" log &&

	# Client issued bundle-uri command
	grep "> command=bundle-uri" log &&

	GIT_TRACE_PACKET="$PWD/log3" \
	git \
		-c transfer.bundleURI=true \
		-c protocol.version=2 \
		clone --bundle-uri="$BUNDLE_URI_BUNDLE_URI" \
		"$BUNDLE_URI_REPO_URI" cloned3 \
		>actual 2>err &&

	# Server responded using protocol v2
	grep "< version 2" log3 &&

	# Server advertised bundle-uri capability
	grep "< bundle-uri" log3 &&

	# Client did not issue bundle-uri command (--bundle-uri override)
	! grep "> command=bundle-uri" log3
'

# The remaining tests will all assume transfer.bundleURI=true
#
# This test can be removed when transfer.bundleURI is enabled by default.
test_expect_success 'enable transfer.bundleURI for remaining tests' '
	git config --global transfer.bundleURI true
'

test_expect_success "test bundle-uri with $BUNDLE_URI_PROTOCOL:// using protocol v2" '
	test_config -C "$BUNDLE_URI_PARENT" \
		bundle.only.uri "$BUNDLE_URI_BUNDLE_URI_ESCAPED" &&

	# All data about bundle URIs
	cat >expect <<-EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "only"]
		uri = $BUNDLE_URI_BUNDLE_URI_ESCAPED
	EOF

	test-tool bundle-uri \
		ls-remote \
		"$BUNDLE_URI_REPO_URI" \
		>actual &&
	test_cmp_config_output expect actual
'

test_expect_success "test bundle-uri with $BUNDLE_URI_PROTOCOL:// using protocol v2 and extra data" '
	test_config -C "$BUNDLE_URI_PARENT" \
		bundle.only.uri "$BUNDLE_URI_BUNDLE_URI_ESCAPED" &&

	# Extra data should be ignored
	test_config -C "$BUNDLE_URI_PARENT" bundle.only.extra bogus &&

	# All data about bundle URIs
	cat >expect <<-EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "only"]
		uri = $BUNDLE_URI_BUNDLE_URI_ESCAPED
	EOF

	test-tool bundle-uri \
		ls-remote \
		"$BUNDLE_URI_REPO_URI" \
		>actual &&
	test_cmp_config_output expect actual
'

test_expect_success "test bundle-uri with $BUNDLE_URI_PROTOCOL:// using protocol v2 with list" '
	test_config -C "$BUNDLE_URI_PARENT" \
		bundle.bundle1.uri "$BUNDLE_URI_BUNDLE_URI_ESCAPED-1.bdl" &&
	test_config -C "$BUNDLE_URI_PARENT" \
		bundle.bundle2.uri "$BUNDLE_URI_BUNDLE_URI_ESCAPED-2.bdl" &&
	test_config -C "$BUNDLE_URI_PARENT" \
		bundle.bundle3.uri "$BUNDLE_URI_BUNDLE_URI_ESCAPED-3.bdl" &&

	# All data about bundle URIs
	cat >expect <<-EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "bundle1"]
		uri = $BUNDLE_URI_BUNDLE_URI_ESCAPED-1.bdl
	[bundle "bundle2"]
		uri = $BUNDLE_URI_BUNDLE_URI_ESCAPED-2.bdl
	[bundle "bundle3"]
		uri = $BUNDLE_URI_BUNDLE_URI_ESCAPED-3.bdl
	EOF

	test-tool bundle-uri \
		ls-remote \
		"$BUNDLE_URI_REPO_URI" \
		>actual &&
	test_cmp_config_output expect actual
'
