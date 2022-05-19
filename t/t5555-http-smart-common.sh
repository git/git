#!/bin/sh

test_description='test functionality common to smart fetch & push'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit --no-tag initial
'

test_expect_success 'but upload-pack --http-backend-info-refs and --advertise-refs are aliased' '
	but upload-pack --http-backend-info-refs . >expected 2>err.expected &&
	but upload-pack --advertise-refs . >actual 2>err.actual &&
	test_cmp err.expected err.actual &&
	test_cmp expected actual
'

test_expect_success 'but receive-pack --http-backend-info-refs and --advertise-refs are aliased' '
	but receive-pack --http-backend-info-refs . >expected 2>err.expected &&
	but receive-pack --advertise-refs . >actual 2>err.actual &&
	test_cmp err.expected err.actual &&
	test_cmp expected actual
'

test_expect_success 'but upload-pack --advertise-refs' '
	cat >expect <<-EOF &&
	$(but rev-parse HEAD) HEAD
	$(but rev-parse HEAD) $(but symbolic-ref HEAD)
	0000
	EOF

	# We only care about BUT_PROTOCOL, not BUT_TEST_PROTOCOL_VERSION
	sane_unset BUT_PROTOCOL &&
	BUT_TEST_PROTOCOL_VERSION=2 \
	but upload-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect &&

	# The --advertise-refs alias works
	but upload-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'but upload-pack --advertise-refs: v0' '
	# With no specified protocol
	cat >expect <<-EOF &&
	$(but rev-parse HEAD) HEAD
	$(but rev-parse HEAD) $(but symbolic-ref HEAD)
	0000
	EOF

	but upload-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect &&

	# With explicit v0
	BUT_PROTOCOL=version=0 \
	but upload-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect

'

test_expect_success 'but receive-pack --advertise-refs: v0' '
	# With no specified protocol
	cat >expect <<-EOF &&
	$(but rev-parse HEAD) $(but symbolic-ref HEAD)
	0000
	EOF

	but receive-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect &&

	# With explicit v0
	BUT_PROTOCOL=version=0 \
	but receive-pack --advertise-refs . >out 2>err &&
	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect

'

test_expect_success 'but upload-pack --advertise-refs: v1' '
	# With no specified protocol
	cat >expect <<-EOF &&
	version 1
	$(but rev-parse HEAD) HEAD
	$(but rev-parse HEAD) $(but symbolic-ref HEAD)
	0000
	EOF

	BUT_PROTOCOL=version=1 \
	but upload-pack --advertise-refs . >out &&

	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'but receive-pack --advertise-refs: v1' '
	# With no specified protocol
	cat >expect <<-EOF &&
	version 1
	$(but rev-parse HEAD) $(but symbolic-ref HEAD)
	0000
	EOF

	BUT_PROTOCOL=version=1 \
	but receive-pack --advertise-refs . >out &&

	test-tool pkt-line unpack <out >actual 2>err &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'but upload-pack --advertise-refs: v2' '
	cat >expect <<-EOF &&
	version 2
	agent=FAKE
	ls-refs=unborn
	fetch=shallow wait-for-done
	server-option
	object-format=$(test_oid algo)
	object-info
	0000
	EOF

	BUT_PROTOCOL=version=2 \
	BUT_USER_AGENT=FAKE \
	but upload-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_expect_success 'but receive-pack --advertise-refs: v2' '
	# There is no v2 yet for receive-pack, implicit v0
	cat >expect <<-EOF &&
	$(but rev-parse HEAD) $(but symbolic-ref HEAD)
	0000
	EOF

	BUT_PROTOCOL=version=2 \
	but receive-pack --advertise-refs . >out 2>err &&

	test-tool pkt-line unpack <out >actual &&
	test_must_be_empty err &&
	test_cmp actual expect
'

test_done
