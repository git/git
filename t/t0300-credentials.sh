#!/bin/sh

test_description='basic credential helper tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

test_expect_success 'setup helper scripts' '
	cat >dump <<-\EOF &&
	whoami=$(echo $0 | sed s/.*git-credential-//)
	echo >&2 "$whoami: $*"
	OIFS=$IFS
	IFS==
	while read key value; do
		echo >&2 "$whoami: $key=$value"
		eval "$key=$value"
	done
	IFS=$OIFS
	EOF

	write_script git-credential-useless <<-\EOF &&
	. ./dump
	exit 0
	EOF

	write_script git-credential-quit <<-\EOF &&
	. ./dump
	echo quit=1
	EOF

	write_script git-credential-verbatim <<-\EOF &&
	user=$1; shift
	pass=$1; shift
	. ./dump
	test -z "$user" || echo username=$user
	test -z "$pass" || echo password=$pass
	EOF

	PATH="$PWD:$PATH"
'

test_expect_success 'credential_fill invokes helper' '
	check fill "verbatim foo bar" <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	EOF
'

test_expect_success 'credential_fill invokes multiple helpers' '
	check fill useless "verbatim foo bar" <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=foo
	password=bar
	--
	useless: get
	useless: protocol=http
	useless: host=example.com
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	EOF
'

test_expect_success 'credential_fill stops when we get a full response' '
	check fill "verbatim one two" "verbatim three four" <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=one
	password=two
	--
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	EOF
'

test_expect_success 'credential_fill continues through partial response' '
	check fill "verbatim one \"\"" "verbatim two three" <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=two
	password=three
	--
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	verbatim: username=one
	EOF
'

test_expect_success 'credential_fill passes along metadata' '
	check fill "verbatim one two" <<-\EOF
	protocol=ftp
	host=example.com
	path=foo.git
	--
	protocol=ftp
	host=example.com
	path=foo.git
	username=one
	password=two
	--
	verbatim: get
	verbatim: protocol=ftp
	verbatim: host=example.com
	verbatim: path=foo.git
	EOF
'

test_expect_success 'credential_approve calls all helpers' '
	check approve useless "verbatim one two" <<-\EOF
	protocol=http
	host=example.com
	username=foo
	password=bar
	--
	--
	useless: store
	useless: protocol=http
	useless: host=example.com
	useless: username=foo
	useless: password=bar
	verbatim: store
	verbatim: protocol=http
	verbatim: host=example.com
	verbatim: username=foo
	verbatim: password=bar
	EOF
'

test_expect_success 'do not bother storing password-less credential' '
	check approve useless <<-\EOF
	protocol=http
	host=example.com
	username=foo
	--
	--
	EOF
'


test_expect_success 'credential_reject calls all helpers' '
	check reject useless "verbatim one two" <<-\EOF
	protocol=http
	host=example.com
	username=foo
	password=bar
	--
	--
	useless: erase
	useless: protocol=http
	useless: host=example.com
	useless: username=foo
	useless: password=bar
	verbatim: erase
	verbatim: protocol=http
	verbatim: host=example.com
	verbatim: username=foo
	verbatim: password=bar
	EOF
'

test_expect_success 'usernames can be preserved' '
	check fill "verbatim \"\" three" <<-\EOF
	protocol=http
	host=example.com
	username=one
	--
	protocol=http
	host=example.com
	username=one
	password=three
	--
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	verbatim: username=one
	EOF
'

test_expect_success 'usernames can be overridden' '
	check fill "verbatim two three" <<-\EOF
	protocol=http
	host=example.com
	username=one
	--
	protocol=http
	host=example.com
	username=two
	password=three
	--
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	verbatim: username=one
	EOF
'

test_expect_success 'do not bother completing already-full credential' '
	check fill "verbatim three four" <<-\EOF
	protocol=http
	host=example.com
	username=one
	password=two
	--
	protocol=http
	host=example.com
	username=one
	password=two
	--
	EOF
'

# We can't test the basic terminal password prompt here because
# getpass() tries too hard to find the real terminal. But if our
# askpass helper is run, we know the internal getpass is working.
test_expect_success 'empty helper list falls back to internal getpass' '
	check fill <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=askpass-username
	password=askpass-password
	--
	askpass: Username for '\''http://example.com'\'':
	askpass: Password for '\''http://askpass-username@example.com'\'':
	EOF
'

test_expect_success 'internal getpass does not ask for known username' '
	check fill <<-\EOF
	protocol=http
	host=example.com
	username=foo
	--
	protocol=http
	host=example.com
	username=foo
	password=askpass-password
	--
	askpass: Password for '\''http://foo@example.com'\'':
	EOF
'

HELPER="!f() {
		cat >/dev/null
		echo username=foo
		echo password=bar
	}; f"
test_expect_success 'respect configured credentials' '
	test_config credential.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=foo
	password=bar
	--
	EOF
'

test_expect_success 'match configured credential' '
	test_config credential.https://example.com.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	path=repo.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	EOF
'

test_expect_success 'do not match configured credential' '
	test_config credential.https://foo.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=https
	host=bar
	--
	protocol=https
	host=bar
	username=askpass-username
	password=askpass-password
	--
	askpass: Username for '\''https://bar'\'':
	askpass: Password for '\''https://askpass-username@bar'\'':
	EOF
'

test_expect_success 'match multiple configured helpers' '
	test_config credential.helper "verbatim \"\" \"\"" &&
	test_config credential.https://example.com.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	path=repo.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	EOF
'

test_expect_success 'match multiple configured helpers with URLs' '
	test_config credential.https://example.com/repo.git.helper "verbatim \"\" \"\"" &&
	test_config credential.https://example.com.helper "$HELPER" &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	path=repo.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	EOF
'

test_expect_success 'match percent-encoded values' '
	test_config credential.https://example.com/%2566.git.helper "$HELPER" &&
	check fill <<-\EOF
	url=https://example.com/%2566.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	EOF
'

test_expect_success 'pull username from config' '
	test_config credential.https://example.com.username foo &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	--
	protocol=https
	host=example.com
	username=foo
	password=askpass-password
	--
	askpass: Password for '\''https://foo@example.com'\'':
	EOF
'

test_expect_success 'honors username from URL over helper (URL)' '
	test_config credential.https://example.com.username bob &&
	test_config credential.https://example.com.helper "verbatim \"\" bar" &&
	check fill <<-\EOF
	url=https://alice@example.com
	--
	protocol=https
	host=example.com
	username=alice
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	verbatim: username=alice
	EOF
'

test_expect_success 'honors username from URL over helper (components)' '
	test_config credential.https://example.com.username bob &&
	test_config credential.https://example.com.helper "verbatim \"\" bar" &&
	check fill <<-\EOF
	protocol=https
	host=example.com
	username=alice
	--
	protocol=https
	host=example.com
	username=alice
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	verbatim: username=alice
	EOF
'

test_expect_success 'last matching username wins' '
	test_config credential.https://example.com/path.git.username bob &&
	test_config credential.https://example.com.username alice &&
	test_config credential.https://example.com.helper "verbatim \"\" bar" &&
	check fill <<-\EOF
	url=https://example.com/path.git
	--
	protocol=https
	host=example.com
	username=alice
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	verbatim: username=alice
	EOF
'

test_expect_success 'http paths can be part of context' '
	check fill "verbatim foo bar" <<-\EOF &&
	protocol=https
	host=example.com
	path=foo.git
	--
	protocol=https
	host=example.com
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	EOF
	test_config credential.https://example.com.useHttpPath true &&
	check fill "verbatim foo bar" <<-\EOF
	protocol=https
	host=example.com
	path=foo.git
	--
	protocol=https
	host=example.com
	path=foo.git
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.com
	verbatim: path=foo.git
	EOF
'

test_expect_success 'context uses urlmatch' '
	test_config "credential.https://*.org.useHttpPath" true &&
	check fill "verbatim foo bar" <<-\EOF
	protocol=https
	host=example.org
	path=foo.git
	--
	protocol=https
	host=example.org
	path=foo.git
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=https
	verbatim: host=example.org
	verbatim: path=foo.git
	EOF
'

test_expect_success 'helpers can abort the process' '
	test_must_fail git \
		-c credential.helper=quit \
		-c credential.helper="verbatim foo bar" \
		credential fill >stdout 2>stderr <<-\EOF &&
	protocol=http
	host=example.com
	EOF
	test_must_be_empty stdout &&
	cat >expect <<-\EOF &&
	quit: get
	quit: protocol=http
	quit: host=example.com
	fatal: credential helper '\''quit'\'' told us to quit
	EOF
	test_i18ncmp expect stderr
'

test_expect_success 'empty helper spec resets helper list' '
	test_config credential.helper "verbatim file file" &&
	check fill "" "verbatim cmdline cmdline" <<-\EOF
	protocol=http
	host=example.com
	--
	protocol=http
	host=example.com
	username=cmdline
	password=cmdline
	--
	verbatim: get
	verbatim: protocol=http
	verbatim: host=example.com
	EOF
'

test_expect_success 'url parser rejects embedded newlines' '
	test_must_fail git credential fill 2>stderr <<-\EOF &&
	url=https://one.example.com?%0ahost=two.example.com/
	EOF
	cat >expect <<-\EOF &&
	warning: url contains a newline in its host component: https://one.example.com?%0ahost=two.example.com/
	fatal: credential url cannot be parsed: https://one.example.com?%0ahost=two.example.com/
	EOF
	test_i18ncmp expect stderr
'

test_expect_success 'host-less URLs are parsed as empty host' '
	check fill "verbatim foo bar" <<-\EOF
	url=cert:///path/to/cert.pem
	--
	protocol=cert
	host=
	path=path/to/cert.pem
	username=foo
	password=bar
	--
	verbatim: get
	verbatim: protocol=cert
	verbatim: host=
	verbatim: path=path/to/cert.pem
	EOF
'

test_expect_success 'credential system refuses to work with missing host' '
	test_must_fail git credential fill 2>stderr <<-\EOF &&
	protocol=http
	EOF
	cat >expect <<-\EOF &&
	fatal: refusing to work with credential missing host field
	EOF
	test_i18ncmp expect stderr
'

test_expect_success 'credential system refuses to work with missing protocol' '
	test_must_fail git credential fill 2>stderr <<-\EOF &&
	host=example.com
	EOF
	cat >expect <<-\EOF &&
	fatal: refusing to work with credential missing protocol field
	EOF
	test_i18ncmp expect stderr
'

test_done
