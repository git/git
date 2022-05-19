#!/bin/sh

test_description='session ID in capabilities'

. ./test-lib.sh

REPO="$(pwd)/repo"
LOCAL_PRISTINE="$(pwd)/local_pristine"

test_expect_success 'setup repos for session ID capability tests' '
	but init "$REPO" &&
	test_cummit -C "$REPO" a &&
	but clone "file://$REPO" "$LOCAL_PRISTINE" &&
	test_cummit -C "$REPO" b
'

for PROTO in 0 1 2
do
	test_expect_success "session IDs not advertised by default (fetch v${PROTO})" '
		test_when_finished "rm -rf local tr2-client-events tr2-server-events" &&
		cp -r "$LOCAL_PRISTINE" local &&
		GIT_TRACE2_EVENT="$(pwd)/tr2-client-events" \
		but -c protocol.version=$PROTO -C local fetch \
			--upload-pack "GIT_TRACE2_EVENT=\"$(pwd)/tr2-server-events\" but-upload-pack" \
			origin &&
		test -z "$(grep \"key\":\"server-sid\" tr2-client-events)" &&
		test -z "$(grep \"key\":\"client-sid\" tr2-server-events)"
	'

	test_expect_success "session IDs not advertised by default (push v${PROTO})" '
		test_when_finished "rm -rf local tr2-client-events tr2-server-events" &&
		test_when_finished "but -C local push --delete origin new-branch" &&
		cp -r "$LOCAL_PRISTINE" local &&
		but -C local pull --no-rebase origin &&
		GIT_TRACE2_EVENT="$(pwd)/tr2-client-events" \
		but -c protocol.version=$PROTO -C local push \
			--receive-pack "GIT_TRACE2_EVENT=\"$(pwd)/tr2-server-events\" but-receive-pack" \
			origin HEAD:new-branch &&
		test -z "$(grep \"key\":\"server-sid\" tr2-client-events)" &&
		test -z "$(grep \"key\":\"client-sid\" tr2-server-events)"
	'
done

test_expect_success 'enable SID advertisement' '
	but -C "$REPO" config transfer.advertiseSID true &&
	but -C "$LOCAL_PRISTINE" config transfer.advertiseSID true
'

for PROTO in 0 1 2
do
	test_expect_success "session IDs advertised (fetch v${PROTO})" '
		test_when_finished "rm -rf local tr2-client-events tr2-server-events" &&
		cp -r "$LOCAL_PRISTINE" local &&
		GIT_TRACE2_EVENT="$(pwd)/tr2-client-events" \
		but -c protocol.version=$PROTO -C local fetch \
			--upload-pack "GIT_TRACE2_EVENT=\"$(pwd)/tr2-server-events\" but-upload-pack" \
			origin &&
		grep \"key\":\"server-sid\" tr2-client-events &&
		grep \"key\":\"client-sid\" tr2-server-events
	'

	test_expect_success "session IDs advertised (push v${PROTO})" '
		test_when_finished "rm -rf local tr2-client-events tr2-server-events" &&
		test_when_finished "but -C local push --delete origin new-branch" &&
		cp -r "$LOCAL_PRISTINE" local &&
		but -C local pull --no-rebase origin &&
		GIT_TRACE2_EVENT="$(pwd)/tr2-client-events" \
		but -c protocol.version=$PROTO -C local push \
			--receive-pack "GIT_TRACE2_EVENT=\"$(pwd)/tr2-server-events\" but-receive-pack" \
			origin HEAD:new-branch &&
		grep \"key\":\"server-sid\" tr2-client-events &&
		grep \"key\":\"client-sid\" tr2-server-events
	'

	test_expect_success "client & server log negotiated version (v${PROTO})" '
		test_when_finished "rm -rf local tr2-client-events tr2-server-events" &&
		cp -r "$LOCAL_PRISTINE" local &&
		GIT_TRACE2_EVENT="$(pwd)/tr2-client-events" \
		but -c protocol.version=$PROTO -C local fetch \
			--upload-pack "GIT_TRACE2_EVENT=\"$(pwd)/tr2-server-events\" but-upload-pack" \
			origin &&
		grep \"key\":\"negotiated-version\",\"value\":\"$PROTO\" tr2-client-events &&
		grep \"key\":\"negotiated-version\",\"value\":\"$PROTO\" tr2-server-events
	'
done

test_done
