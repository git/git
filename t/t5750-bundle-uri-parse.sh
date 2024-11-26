#!/bin/sh

test_description="Test bundle-uri bundle_uri_parse_line()"

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

test_expect_success 'bundle_uri_parse_line() just URIs' '
	cat >in <<-\EOF &&
	bundle.one.uri=http://example.com/bundle.bdl
	bundle.two.uri=https://example.com/bundle.bdl
	bundle.three.uri=file:///usr/share/git/bundle.bdl
	EOF

	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = http://example.com/bundle.bdl
	[bundle "two"]
		uri = https://example.com/bundle.bdl
	[bundle "three"]
		uri = file:///usr/share/git/bundle.bdl
	EOF

	test-tool bundle-uri parse-key-values in >actual 2>err &&
	test_must_be_empty err &&
	test_cmp_config_output expect actual
'

test_expect_success 'bundle_uri_parse_line(): relative URIs' '
	cat >in <<-\EOF &&
	bundle.one.uri=bundle.bdl
	bundle.two.uri=../bundle.bdl
	bundle.three.uri=sub/dir/bundle.bdl
	EOF

	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = <uri>/bundle.bdl
	[bundle "two"]
		uri = bundle.bdl
	[bundle "three"]
		uri = <uri>/sub/dir/bundle.bdl
	EOF

	test-tool bundle-uri parse-key-values in >actual 2>err &&
	test_must_be_empty err &&
	test_cmp_config_output expect actual
'

test_expect_success 'bundle_uri_parse_line(): relative URIs and parent paths' '
	cat >in <<-\EOF &&
	bundle.one.uri=bundle.bdl
	bundle.two.uri=../bundle.bdl
	bundle.three.uri=../../bundle.bdl
	EOF

	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = <uri>/bundle.bdl
	[bundle "two"]
		uri = bundle.bdl
	[bundle "three"]
		uri = <uri>/../bundle.bdl
	EOF

	# TODO: We would prefer if parsing a bundle list would not cause
	# a die() and instead would give a warning and allow the rest of
	# a Git command to continue. This test_must_fail is necessary for
	# now until the interface for relative_url() allows for reporting
	# an error instead of die()ing.
	test_must_fail test-tool bundle-uri parse-key-values in >actual 2>err &&
	grep "fatal: cannot strip one component off url" err
'

test_expect_success 'bundle_uri_parse_line() parsing edge cases: empty key or value' '
	cat >in <<-\EOF &&
	=bogus-value
	bogus-key=
	EOF

	cat >err.expect <<-EOF &&
	error: bundle-uri: line has empty key or value
	error: bad line: '\''=bogus-value'\''
	error: bundle-uri: line has empty key or value
	error: bad line: '\''bogus-key='\''
	EOF

	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	EOF

	test_must_fail test-tool bundle-uri parse-key-values in >actual 2>err &&
	test_cmp err.expect err &&
	test_cmp_config_output expect actual
'

test_expect_success 'bundle_uri_parse_line() parsing edge cases: empty lines' '
	cat >in <<-\EOF &&
	bundle.one.uri=http://example.com/bundle.bdl

	bundle.two.uri=https://example.com/bundle.bdl

	bundle.three.uri=file:///usr/share/git/bundle.bdl
	EOF

	cat >err.expect <<-\EOF &&
	error: bundle-uri: got an empty line
	error: bad line: '\'''\''
	error: bundle-uri: got an empty line
	error: bad line: '\'''\''
	EOF

	# We fail, but try to continue parsing regardless
	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = http://example.com/bundle.bdl
	[bundle "two"]
		uri = https://example.com/bundle.bdl
	[bundle "three"]
		uri = file:///usr/share/git/bundle.bdl
	EOF

	test_must_fail test-tool bundle-uri parse-key-values in >actual 2>err &&
	test_cmp err.expect err &&
	test_cmp_config_output expect actual
'

test_expect_success 'bundle_uri_parse_line() parsing edge cases: duplicate lines' '
	cat >in <<-\EOF &&
	bundle.one.uri=http://example.com/bundle.bdl
	bundle.two.uri=https://example.com/bundle.bdl
	bundle.one.uri=https://example.com/bundle-2.bdl
	bundle.three.uri=file:///usr/share/git/bundle.bdl
	EOF

	cat >err.expect <<-\EOF &&
	error: bad line: '\''bundle.one.uri=https://example.com/bundle-2.bdl'\''
	EOF

	# We fail, but try to continue parsing regardless
	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = http://example.com/bundle.bdl
	[bundle "two"]
		uri = https://example.com/bundle.bdl
	[bundle "three"]
		uri = file:///usr/share/git/bundle.bdl
	EOF

	test_must_fail test-tool bundle-uri parse-key-values in >actual 2>err &&
	test_cmp err.expect err &&
	test_cmp_config_output expect actual
'

test_expect_success 'parse config format: just URIs' '
	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = http://example.com/bundle.bdl
	[bundle "two"]
		uri = https://example.com/bundle.bdl
	[bundle "three"]
		uri = file:///usr/share/git/bundle.bdl
	EOF

	test-tool bundle-uri parse-config expect >actual 2>err &&
	test_must_be_empty err &&
	test_cmp_config_output expect actual
'

test_expect_success 'parse config format: relative URIs' '
	cat >in <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = bundle.bdl
	[bundle "two"]
		uri = ../bundle.bdl
	[bundle "three"]
		uri = sub/dir/bundle.bdl
	EOF

	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	[bundle "one"]
		uri = <uri>/bundle.bdl
	[bundle "two"]
		uri = bundle.bdl
	[bundle "three"]
		uri = <uri>/sub/dir/bundle.bdl
	EOF

	test-tool bundle-uri parse-config in >actual 2>err &&
	test_must_be_empty err &&
	test_cmp_config_output expect actual
'

test_expect_success 'parse config format edge cases: empty key or value' '
	cat >in1 <<-\EOF &&
	= bogus-value
	EOF

	cat >err1 <<-EOF &&
	error: bad config line 1 in file in1
	EOF

	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
	EOF

	test_must_fail test-tool bundle-uri parse-config in1 >actual 2>err &&
	test_cmp err1 err &&
	test_cmp_config_output expect actual &&

	cat >in2 <<-\EOF &&
	bogus-key =
	EOF

	cat >err2 <<-EOF &&
	error: bad config line 1 in file in2
	EOF

	test_must_fail test-tool bundle-uri parse-config in2 >actual 2>err &&
	test_cmp err2 err &&
	test_cmp_config_output expect actual
'

test_expect_success 'parse config format: creationToken heuristic' '
	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken
	[bundle "one"]
		uri = http://example.com/bundle.bdl
		creationToken = 123456
	[bundle "two"]
		uri = https://example.com/bundle.bdl
		creationToken = 12345678901234567890
	[bundle "three"]
		uri = file:///usr/share/git/bundle.bdl
		creationToken = 1
	EOF

	test-tool bundle-uri parse-config expect >actual 2>err &&
	test_must_be_empty err &&
	test_cmp_config_output expect actual
'

test_expect_success 'parse config format edge cases: creationToken heuristic' '
	cat >expect <<-\EOF &&
	[bundle]
		version = 1
		mode = all
		heuristic = creationToken
	[bundle "one"]
		uri = http://example.com/bundle.bdl
		creationToken = bogus
	EOF

	test-tool bundle-uri parse-config expect >actual 2>err &&
	grep "could not parse bundle list key creationToken with value '\''bogus'\''" err
'

test_done
