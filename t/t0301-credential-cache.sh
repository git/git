#!/bin/sh

test_description='credential-cache tests'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-credential.sh

test -z "$NO_UNIX_SOCKETS" || {
	skip_all='skipping credential-cache tests, unix sockets not available'
	test_done
}

uname_s=$(uname -s)
case $uname_s in
*MINGW*)
	test_path_is_socket () {
		# `test -S` cannot detect Win10's Unix sockets
		test_path_exists "$1"
	}
	;;
*)
	test_path_is_socket () {
		test -S "$1"
	}
	;;
esac

# don't leave a stale daemon running
test_atexit 'git credential-cache exit'

# test that the daemon works with no special setup
helper_test cache
helper_test_oauth_refresh_token cache

test_expect_success 'socket defaults to ~/.cache/git/credential/socket' '
	test_when_finished "
		git credential-cache exit &&
		rmdir -p .cache/git/credential/
	" &&
	test_path_is_missing "$HOME/.git-credential-cache" &&
	test_path_is_socket "$HOME/.cache/git/credential/socket"
'

XDG_CACHE_HOME="$HOME/xdg"
export XDG_CACHE_HOME
# test behavior when XDG_CACHE_HOME is set
helper_test cache

test_expect_success "use custom XDG_CACHE_HOME if set and default sockets are not created" '
	test_when_finished "git credential-cache exit" &&
	test_path_is_socket "$XDG_CACHE_HOME/git/credential/socket" &&
	test_path_is_missing "$HOME/.git-credential-cache/socket" &&
	test_path_is_missing "$HOME/.cache/git/credential/socket"
'
unset XDG_CACHE_HOME

test_expect_success 'credential-cache --socket option overrides default location' '
	test_when_finished "
		git credential-cache exit --socket \"\$HOME/dir/socket\" &&
		rmdir \"\$HOME/dir\"
	" &&
	check approve "cache --socket \"\$HOME/dir/socket\"" <<-\EOF &&
	protocol=https
	host=example.com
	username=store-user
	password=store-pass
	EOF
	test_path_is_socket "$HOME/dir/socket"
'

test_expect_success "use custom XDG_CACHE_HOME even if xdg socket exists" '
	test_when_finished "
		git credential-cache exit &&
		sane_unset XDG_CACHE_HOME
	" &&
	check approve cache <<-\EOF &&
	protocol=https
	host=example.com
	username=store-user
	password=store-pass
	EOF
	test_path_is_socket "$HOME/.cache/git/credential/socket" &&
	XDG_CACHE_HOME="$HOME/xdg" &&
	export XDG_CACHE_HOME &&
	check approve cache <<-\EOF &&
	protocol=https
	host=example.com
	username=store-user
	password=store-pass
	EOF
	test_path_is_socket "$XDG_CACHE_HOME/git/credential/socket"
'

test_expect_success 'use user socket if user directory exists' '
	test_when_finished "
		git credential-cache exit &&
		rmdir \"\$HOME/.git-credential-cache/\"
	" &&
	mkdir -p "$HOME/.git-credential-cache/" &&
	chmod 700 "$HOME/.git-credential-cache/" &&
	check approve cache <<-\EOF &&
	protocol=https
	host=example.com
	username=store-user
	password=store-pass
	EOF
	test_path_is_socket "$HOME/.git-credential-cache/socket"
'

test_expect_success SYMLINKS 'use user socket if user directory is a symlink to a directory' '
	test_when_finished "
		git credential-cache exit &&
		rmdir \"\$HOME/dir/\" &&
		rm \"\$HOME/.git-credential-cache\"
	" &&
	mkdir -p -m 700 "$HOME/dir/" &&
	ln -s "$HOME/dir" "$HOME/.git-credential-cache" &&
	check approve cache <<-\EOF &&
	protocol=https
	host=example.com
	username=store-user
	password=store-pass
	EOF
	test_path_is_socket "$HOME/.git-credential-cache/socket"
'

helper_test_timeout cache --timeout=1

test_done
