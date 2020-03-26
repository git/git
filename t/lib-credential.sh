# Shell library for testing credential handling including helpers. See t0302
# for an example of testing a specific helper.

# Try a set of credential helpers; the expected stdin,
# stdout and stderr should be provided on stdin,
# separated by "--".
check() {
	credential_opts=
	credential_cmd=$1
	shift
	for arg in "$@"; do
		credential_opts="$credential_opts -c credential.helper='$arg'"
	done
	read_chunk >stdin &&
	read_chunk >expect-stdout &&
	read_chunk >expect-stderr &&
	if ! eval "git $credential_opts credential $credential_cmd <stdin >stdout 2>stderr"; then
		echo "git credential failed with code $?" &&
		cat stderr &&
		false
	fi &&
	test_cmp expect-stdout stdout &&
	test_cmp expect-stderr stderr
}

read_chunk() {
	while read line; do
		case "$line" in
		--) break ;;
		*) echo "$line" ;;
		esac
	done
}

# Clear any residual data from previous tests. We only
# need this when testing third-party helpers which read and
# write outside of our trash-directory sandbox.
#
# Don't bother checking for success here, as it is
# outside the scope of tests and represents a best effort to
# clean up after ourselves.
helper_test_clean() {
	reject $1 https example.com store-user
	reject $1 https example.com user1
	reject $1 https example.com user2
	reject $1 http path.tld user
	reject $1 https timeout.tld user
	reject $1 https sso.tld
}

reject() {
	(
		echo protocol=$2
		echo host=$3
		echo username=$4
	) | git -c credential.helper=$1 credential reject
}

helper_test() {
	HELPER=$1

	test_expect_success "helper ($HELPER) has no existing data" '
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		--
		protocol=https
		host=example.com
		username=askpass-username
		password=askpass-password
		--
		askpass: Username for '\''https://example.com'\'':
		askpass: Password for '\''https://askpass-username@example.com'\'':
		EOF
	'

	test_expect_success "helper ($HELPER) stores password" '
		check approve $HELPER <<-\EOF
		protocol=https
		host=example.com
		username=store-user
		password=store-pass
		EOF
	'

	test_expect_success "helper ($HELPER) can retrieve password" '
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		--
		protocol=https
		host=example.com
		username=store-user
		password=store-pass
		--
		EOF
	'

	test_expect_success "helper ($HELPER) requires matching protocol" '
		check fill $HELPER <<-\EOF
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

	test_expect_success "helper ($HELPER) requires matching host" '
		check fill $HELPER <<-\EOF
		protocol=https
		host=other.tld
		--
		protocol=https
		host=other.tld
		username=askpass-username
		password=askpass-password
		--
		askpass: Username for '\''https://other.tld'\'':
		askpass: Password for '\''https://askpass-username@other.tld'\'':
		EOF
	'

	test_expect_success "helper ($HELPER) requires matching username" '
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		username=other
		--
		protocol=https
		host=example.com
		username=other
		password=askpass-password
		--
		askpass: Password for '\''https://other@example.com'\'':
		EOF
	'

	test_expect_success "helper ($HELPER) requires matching path" '
		test_config credential.usehttppath true &&
		check approve $HELPER <<-\EOF &&
		protocol=http
		host=path.tld
		path=foo.git
		username=user
		password=pass
		EOF
		check fill $HELPER <<-\EOF
		protocol=http
		host=path.tld
		path=bar.git
		--
		protocol=http
		host=path.tld
		path=bar.git
		username=askpass-username
		password=askpass-password
		--
		askpass: Username for '\''http://path.tld/bar.git'\'':
		askpass: Password for '\''http://askpass-username@path.tld/bar.git'\'':
		EOF
	'

	test_expect_success "helper ($HELPER) can forget host" '
		check reject $HELPER <<-\EOF &&
		protocol=https
		host=example.com
		EOF
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		--
		protocol=https
		host=example.com
		username=askpass-username
		password=askpass-password
		--
		askpass: Username for '\''https://example.com'\'':
		askpass: Password for '\''https://askpass-username@example.com'\'':
		EOF
	'

	test_expect_success "helper ($HELPER) can store multiple users" '
		check approve $HELPER <<-\EOF &&
		protocol=https
		host=example.com
		username=user1
		password=pass1
		EOF
		check approve $HELPER <<-\EOF &&
		protocol=https
		host=example.com
		username=user2
		password=pass2
		EOF
		check fill $HELPER <<-\EOF &&
		protocol=https
		host=example.com
		username=user1
		--
		protocol=https
		host=example.com
		username=user1
		password=pass1
		EOF
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		username=user2
		--
		protocol=https
		host=example.com
		username=user2
		password=pass2
		EOF
	'

	test_expect_success "helper ($HELPER) can forget user" '
		check reject $HELPER <<-\EOF &&
		protocol=https
		host=example.com
		username=user1
		EOF
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		username=user1
		--
		protocol=https
		host=example.com
		username=user1
		password=askpass-password
		--
		askpass: Password for '\''https://user1@example.com'\'':
		EOF
	'

	test_expect_success "helper ($HELPER) remembers other user" '
		check fill $HELPER <<-\EOF
		protocol=https
		host=example.com
		username=user2
		--
		protocol=https
		host=example.com
		username=user2
		password=pass2
		EOF
	'

	test_expect_success "helper ($HELPER) can store empty username" '
		check approve $HELPER <<-\EOF &&
		protocol=https
		host=sso.tld
		username=
		password=
		EOF
		check fill $HELPER <<-\EOF
		protocol=https
		host=sso.tld
		--
		protocol=https
		host=sso.tld
		username=
		password=
		EOF
	'
}

helper_test_timeout() {
	HELPER="$*"

	test_expect_success "helper ($HELPER) times out" '
		check approve "$HELPER" <<-\EOF &&
		protocol=https
		host=timeout.tld
		username=user
		password=pass
		EOF
		sleep 2 &&
		check fill "$HELPER" <<-\EOF
		protocol=https
		host=timeout.tld
		--
		protocol=https
		host=timeout.tld
		username=askpass-username
		password=askpass-password
		--
		askpass: Username for '\''https://timeout.tld'\'':
		askpass: Password for '\''https://askpass-username@timeout.tld'\'':
		EOF
	'
}

write_script askpass <<\EOF
echo >&2 askpass: $*
what=$(echo $1 | cut -d" " -f1 | tr A-Z a-z | tr -cd a-z)
echo "askpass-$what"
EOF
GIT_ASKPASS="$PWD/askpass"
export GIT_ASKPASS
