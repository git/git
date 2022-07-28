#!/bin/sh

test_description='test functionality common to smart fetch & push'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit --no-tag initial
'

test_expect_success 'git upload-pack --http-backend-info-refs and --advertise-refs are aliased' '
	git upload-pack --http-backend-info-refs . >expected 2>err.expected &&
	git upload-pack --advertise-refs . >actual 2>err.actual &&
	test_cmp err.expected err.actual &&
	test_cmp expected actual
'

test_expect_success 'git receive-pack --http-backend-info-refs and --advertise-refs are aliased' '
	git receive-pack --http-backend-info-refs . >expected 2>err.expected &&
	git receive-pack --advertise-refs . >actual 2>err.actual &&
	test_cmp err.expected err.actual &&
	test_cmp expected actual
'

test_expect_success 'git upload-pack --advertise-refs' '
	cat >expect <<-EOF &&
	$(git rev-parse HEAD) HEAD
	$(git rev-parse HEAD) $(git symbolic-ref HEAD)
	0000
	EOF

	# We only care about GIT_PROTOCOL, not GIT_TEST_PROTOCOL_VERSION
	sane_unset GIT_PROTOCOL &&
	GIT_TEST_PROTOCOL_VERSION=2 \
	git upload-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect &&

	# The --advertise-refs alias works
	git upload-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'git upload-pack --advertise-refs: v0' '
	# With no specified protocol
	cat >expect <<-EOF &&
	$(git rev-parse HEAD) HEAD
	$(git rev-parse HEAD) $(git symbolic-ref HEAD)
	0000
	EOF

	git upload-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect &&

	# With explicit v0
	GIT_PROTOCOL=version=0 \
	git upload-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect

'

test_expect_success 'git receive-pack --advertise-refs: v0' '
	# With no specified protocol
	cat >expect <<-EOF &&
	$(git rev-parse HEAD) $(git symbolic-ref HEAD)
	0000
	EOF

	git receive-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect &&

	# With explicit v0
	GIT_PROTOCOL=version=0 \
	git receive-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect

'

test_expect_success 'git upload-pack --advertise-refs: v1' '
	# With no specified protocol
	cat >expect <<-EOF &&
	version 1
	$(git rev-parse HEAD) HEAD
	$(git rev-parse HEAD) $(git symbolic-ref HEAD)
	0000
	EOF

	GIT_PROTOCOL=version=1 \
	git upload-pack --advertise-refs . >out &&

	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'git receive-pack --advertise-refs: v1' '
	# With no specified protocol
	cat >expect <<-EOF &&
	version 1
	$(git rev-parse HEAD) $(git symbolic-ref HEAD)
	0000
	EOF

	GIT_PROTOCOL=version=1 \
	git receive-pack --advertise-refs . >out &&

	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'git upload-pack --advertise-refs: v2' '
	cat >expect <<-EOF &&
	version 2
	agent=FAKE
	ls-refs=unborn
	fetch=shallow wait-for-done
	server-option
	object-format=$(test_oid algo)
	object-info=size
	0000
	EOF

	GIT_PROTOCOL=version=2 \
	GIT_USER_AGENT=FAKE \
	git upload-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'git receive-pack --advertise-refs: v2' '
	# There is no v2 yet for receive-pack, implicit v0
	cat >expect <<-EOF &&
	$(git rev-parse HEAD) $(git symbolic-ref HEAD)
	0000
	EOF

	GIT_PROTOCOL=version=2 \
	git receive-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_done
