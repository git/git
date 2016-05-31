# Test routines for checking protocol disabling.

# test cloning a particular protocol
#   $1 - description of the protocol
#   $2 - machine-readable name of the protocol
#   $3 - the URL to try cloning
test_proto () {
	desc=$1
	proto=$2
	url=$3

	test_expect_success "clone $1 (enabled)" '
		rm -rf tmp.git &&
		(
			GIT_ALLOW_PROTOCOL=$proto &&
			export GIT_ALLOW_PROTOCOL &&
			git clone --bare "$url" tmp.git
		)
	'

	test_expect_success "fetch $1 (enabled)" '
		(
			cd tmp.git &&
			GIT_ALLOW_PROTOCOL=$proto &&
			export GIT_ALLOW_PROTOCOL &&
			git fetch
		)
	'

	test_expect_success "push $1 (enabled)" '
		(
			cd tmp.git &&
			GIT_ALLOW_PROTOCOL=$proto &&
			export GIT_ALLOW_PROTOCOL &&
			git push origin HEAD:pushed
		)
	'

	test_expect_success "push $1 (disabled)" '
		(
			cd tmp.git &&
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail git push origin HEAD:pushed
		)
	'

	test_expect_success "fetch $1 (disabled)" '
		(
			cd tmp.git &&
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail git fetch
		)
	'

	test_expect_success "clone $1 (disabled)" '
		rm -rf tmp.git &&
		(
			GIT_ALLOW_PROTOCOL=none &&
			export GIT_ALLOW_PROTOCOL &&
			test_must_fail git clone --bare "$url" tmp.git
		)
	'
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
# like "ext::fake-remote %S repo.git"
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
