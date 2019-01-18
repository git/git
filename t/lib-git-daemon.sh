# Shell library to run git-daemon in tests.  Ends the test early if
# GIT_TEST_GIT_DAEMON is not set.
#
# Usage:
#
#	. ./test-lib.sh
#	. "$TEST_DIRECTORY"/lib-git-daemon.sh
#	start_git_daemon
#
#	test_expect_success '...' '
#		...
#	'
#
#	test_expect_success ...
#
#	stop_git_daemon
#	test_done

test_tristate GIT_TEST_GIT_DAEMON
if test "$GIT_TEST_GIT_DAEMON" = false
then
	skip_all="git-daemon testing disabled (unset GIT_TEST_GIT_DAEMON to enable)"
	test_done
fi

if test_have_prereq !PIPE
then
	test_skip_or_die $GIT_TEST_GIT_DAEMON "file system does not support FIFOs"
fi

test_set_port LIB_GIT_DAEMON_PORT

GIT_DAEMON_PID=
GIT_DAEMON_DOCUMENT_ROOT_PATH="$PWD"/repo
GIT_DAEMON_HOST_PORT=127.0.0.1:$LIB_GIT_DAEMON_PORT
GIT_DAEMON_URL=git://$GIT_DAEMON_HOST_PORT

start_git_daemon() {
	if test -n "$GIT_DAEMON_PID"
	then
		error "start_git_daemon already called"
	fi

	mkdir -p "$GIT_DAEMON_DOCUMENT_ROOT_PATH"

	trap 'code=$?; stop_git_daemon; (exit $code); die' EXIT

	say >&3 "Starting git daemon ..."
	mkfifo git_daemon_output
	${LIB_GIT_DAEMON_COMMAND:-git daemon} \
		--listen=127.0.0.1 --port="$LIB_GIT_DAEMON_PORT" \
		--reuseaddr --verbose \
		--base-path="$GIT_DAEMON_DOCUMENT_ROOT_PATH" \
		"$@" "$GIT_DAEMON_DOCUMENT_ROOT_PATH" \
		>&3 2>git_daemon_output &
	GIT_DAEMON_PID=$!
	{
		read -r line <&7
		printf "%s\n" "$line" >&4
		cat <&7 >&4 &
	} 7<git_daemon_output &&

	# Check expected output
	if test x"$(expr "$line" : "\[[0-9]*\] \(.*\)")" != x"Ready to rumble"
	then
		kill "$GIT_DAEMON_PID"
		wait "$GIT_DAEMON_PID"
		trap 'die' EXIT
		test_skip_or_die $GIT_TEST_GIT_DAEMON \
			"git daemon failed to start"
	fi
}

stop_git_daemon() {
	if test -z "$GIT_DAEMON_PID"
	then
		return
	fi

	trap 'die' EXIT

	# kill git-daemon child of git
	say >&3 "Stopping git daemon ..."
	kill "$GIT_DAEMON_PID"
	wait "$GIT_DAEMON_PID" >&3 2>&4
	ret=$?
	if ! test_match_signal 15 $ret
	then
		error "git daemon exited with status: $ret"
	fi
	GIT_DAEMON_PID=
	rm -f git_daemon_output
}

# A stripped-down version of a netcat client, that connects to a "host:port"
# given in $1, sends its stdin followed by EOF, then dumps the response (until
# EOF) to stdout.
fake_nc() {
	if ! test_declared_prereq FAKENC
	then
		echo >&4 "fake_nc: need to declare FAKENC prerequisite"
		return 127
	fi
	perl -Mstrict -MIO::Socket::INET -e '
		my $s = IO::Socket::INET->new(shift)
			or die "unable to open socket: $!";
		print $s <STDIN>;
		$s->shutdown(1);
		print <$s>;
	' "$@"
}

test_lazy_prereq FAKENC '
	perl -MIO::Socket::INET -e "exit 0"
'
