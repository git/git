# Test routines for checking protocol disabling.

# Test clone/fetch/push with GIT_ALLOW_PROTOCOL whitelist
test_whitelist () {
	desc=$1
	proto=$2
	url=$3

	test_expect_success "clone $desc (enabled)" '
		rm -rf tmp.but &&
		(
			GIT_ALLOW_PROTOCOL=$proto &&
			export GIT_ALLOW_PROTOCOL &&
			but clone --bare "$url" tmp.but
		)
	'

	test_expect_success "fetch $desc (enabled)" '
		(
			cd tmp.but &&
			GIT_ALLOW_PROTOCOL=$proto &&
			export GIT_ALLOW_PROTOCOL &&
			but fetch
		)
	'

	test_expect_success "push $desc (enabled)" '
		(
			cd tmp.but &&
			GIT_ALLOW_PROTOCOL=$proto &&
			export GIT_ALLOW_PROTOCOL &&
			but push origin HEAD:pushed
		)
	'

	test_expect_success "push $desc (disabled)" '
		(
			cd tmp.but &&
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail but push origin HEAD:pushed
		)
	'

	test_expect_success "fetch $desc (disabled)" '
		(
			cd tmp.but &&
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail but fetch
		)
	'

	test_expect_success "clone $desc (disabled)" '
		rm -rf tmp.but &&
		(
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail but clone --bare "$url" tmp.but
		)
	'

	test_expect_success "clone $desc (env var has precedence)" '
		rm -rf tmp.but &&
		(
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail but -c protocol.allow=always clone --bare "$url" tmp.but &&
			test_must_fail but -c protocol.$proto.allow=always clone --bare "$url" tmp.but
		)
	'
}

test_config () {
	desc=$1
	proto=$2
	url=$3

	# Test clone/fetch/push with protocol.<type>.allow config
	test_expect_success "clone $desc (enabled with config)" '
		rm -rf tmp.but &&
		but -c protocol.$proto.allow=always clone --bare "$url" tmp.but
	'

	test_expect_success "fetch $desc (enabled)" '
		but -C tmp.but -c protocol.$proto.allow=always fetch
	'

	test_expect_success "push $desc (enabled)" '
		but -C tmp.but -c protocol.$proto.allow=always  push origin HEAD:pushed
	'

	test_expect_success "push $desc (disabled)" '
		test_must_fail but -C tmp.but -c protocol.$proto.allow=never push origin HEAD:pushed
	'

	test_expect_success "fetch $desc (disabled)" '
		test_must_fail but -C tmp.but -c protocol.$proto.allow=never fetch
	'

	test_expect_success "clone $desc (disabled)" '
		rm -rf tmp.but &&
		test_must_fail but -c protocol.$proto.allow=never clone --bare "$url" tmp.but
	'

	# Test clone/fetch/push with protocol.user.allow and its env var
	test_expect_success "clone $desc (enabled)" '
		rm -rf tmp.but &&
		but -c protocol.$proto.allow=user clone --bare "$url" tmp.but
	'

	test_expect_success "fetch $desc (enabled)" '
		but -C tmp.but -c protocol.$proto.allow=user fetch
	'

	test_expect_success "push $desc (enabled)" '
		but -C tmp.but -c protocol.$proto.allow=user push origin HEAD:pushed
	'

	test_expect_success "push $desc (disabled)" '
		(
			cd tmp.but &&
			GIT_PROTOCOL_FROM_USER=0 &&
			export GIT_PROTOCOL_FROM_USER &&
			test_must_fail but -c protocol.$proto.allow=user push origin HEAD:pushed
		)
	'

	test_expect_success "fetch $desc (disabled)" '
		(
			cd tmp.but &&
			GIT_PROTOCOL_FROM_USER=0 &&
			export GIT_PROTOCOL_FROM_USER &&
			test_must_fail but -c protocol.$proto.allow=user fetch
		)
	'

	test_expect_success "clone $desc (disabled)" '
		rm -rf tmp.but &&
		(
			GIT_PROTOCOL_FROM_USER=0 &&
			export GIT_PROTOCOL_FROM_USER &&
			test_must_fail but -c protocol.$proto.allow=user clone --bare "$url" tmp.but
		)
	'

	# Test clone/fetch/push with protocol.allow user defined default
	test_expect_success "clone $desc (enabled)" '
		rm -rf tmp.but &&
		test_config_global protocol.allow always &&
		but clone --bare "$url" tmp.but
	'

	test_expect_success "fetch $desc (enabled)" '
		test_config_global protocol.allow always &&
		but -C tmp.but fetch
	'

	test_expect_success "push $desc (enabled)" '
		test_config_global protocol.allow always &&
		but -C tmp.but push origin HEAD:pushed
	'

	test_expect_success "push $desc (disabled)" '
		test_config_global protocol.allow never &&
		test_must_fail but -C tmp.but push origin HEAD:pushed
	'

	test_expect_success "fetch $desc (disabled)" '
		test_config_global protocol.allow never &&
		test_must_fail but -C tmp.but fetch
	'

	test_expect_success "clone $desc (disabled)" '
		rm -rf tmp.but &&
		test_config_global protocol.allow never &&
		test_must_fail but clone --bare "$url" tmp.but
	'
}

# test cloning a particular protocol
#   $1 - description of the protocol
#   $2 - machine-readable name of the protocol
#   $3 - the URL to try cloning
test_proto () {
	test_whitelist "$@"

	test_config "$@"
}

# set up an ssh wrapper that will access $host/$repo in the
# trash directory, and enable it for subsequent tests.
setup_ssh_wrapper () {
	test_expect_success 'setup ssh wrapper' '
		write_script ssh-wrapper <<-\EOF &&
		echo >&2 "ssh: $*"
		host=$1; shift
		cd "$TRASH_DIRECTORY/$host" &&
		eval "$*"
		EOF
		GIT_SSH="$PWD/ssh-wrapper" &&
		export GIT_SSH &&
		export TRASH_DIRECTORY
	'
}

# set up a wrapper that can be used with remote-ext to
# access repositories in the "remote" directory of trash-dir,
# like "ext::fake-remote %S repo.but"
setup_ext_wrapper () {
	test_expect_success 'setup ext wrapper' '
		write_script fake-remote <<-\EOF &&
		echo >&2 "fake-remote: $*"
		cd "$TRASH_DIRECTORY/remote" &&
		eval "$*"
		EOF
		PATH=$TRASH_DIRECTORY:$PATH &&
		export TRASH_DIRECTORY
	'
}
