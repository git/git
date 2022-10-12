#!/bin/sh

test_description="Test bundle-uri bundle_uri_parse_line()"

TEST_NO_CREATE_REPO=1
TEST_PASSES_SANITIZE_LEAK=true
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

test_done
