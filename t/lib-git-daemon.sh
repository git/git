# Shell library to run but-daemon in tests.  Ends the test early if
# GIT_TEST_GIT_DAEMON is not set.
#
# Usage:
#
#	. ./test-lib.sh
#	. "$TEST_DIRECTORY"/lib-but-daemon.sh
#	start_but_daemon
#
#	test_expect_success '...' '
#		...
#	'
#
#	test_expect_success ...
#
#	test_done

if ! test_bool_env GIT_TEST_GIT_DAEMON true
then
	skip_all="but-daemon testing disabled (unset GIT_TEST_GIT_DAEMON to enable)"
	test_done
fi

if test_have_prereq !PIPE
then
	test_skip_or_die GIT_TEST_GIT_DAEMON "file system does not support FIFOs"
fi

test_set_port LIB_GIT_DAEMON_PORT

GIT_DAEMON_PID=
GIT_DAEMON_PIDFILE="$PWD"/daemon.pid
GIT_DAEMON_DOCUMENT_ROOT_PATH="$PWD"/repo
GIT_DAEMON_HOST_PORT=127.0.0.1:$LIB_GIT_DAEMON_PORT
GIT_DAEMON_URL=but://$GIT_DAEMON_HOST_PORT

registered_stop_but_daemon_atexit_handler=
start_but_daemon() {
	if test -n "$GIT_DAEMON_PID"
	then
		error "start_but_daemon already called"
	fi

	mkdir -p "$GIT_DAEMON_DOCUMENT_ROOT_PATH"

	# One of the test scripts stops and then re-starts 'but daemon'.
	# Don't register and then run the same atexit handlers several times.
	if test -z "$registered_stop_but_daemon_atexit_handler"
	then
		test_atexit 'stop_but_daemon'
		registered_stop_but_daemon_atexit_handler=AlreadyDone
	fi

	say >&3 "Starting but daemon ..."
	mkfifo but_daemon_output
	${LIB_GIT_DAEMON_COMMAND:-but daemon} \
		--listen=127.0.0.1 --port="$LIB_GIT_DAEMON_PORT" \
		--reuseaddr --verbose --pid-file="$GIT_DAEMON_PIDFILE" \
		--base-path="$GIT_DAEMON_DOCUMENT_ROOT_PATH" \
		"$@" "$GIT_DAEMON_DOCUMENT_ROOT_PATH" \
		>&3 2>but_daemon_output &
	GIT_DAEMON_PID=$!
	{
		read -r line <&7
		printf "%s\n" "$line" >&4
		cat <&7 >&4 &
	} 7<but_daemon_output &&

	# Check expected output
	if test x"$(expr "$line" : "\[[0-9]*\] \(.*\)")" != x"Ready to rumble"
	then
		kill "$GIT_DAEMON_PID"
		wait "$GIT_DAEMON_PID"
		unset GIT_DAEMON_PID
		test_skip_or_die GIT_TEST_GIT_DAEMON \
			"but daemon failed to start"
	fi
}

stop_but_daemon() {
	if test -z "$GIT_DAEMON_PID"
	then
		return
	fi

	# kill but-daemon child of but
	say >&3 "Stopping but daemon ..."
	kill "$GIT_DAEMON_PID"
	wait "$GIT_DAEMON_PID" >&3 2>&4
	ret=$?
	if ! test_match_signal 15 $ret
	then
		error "but daemon exited with status: $ret"
	fi
	kill "$(cat "$GIT_DAEMON_PIDFILE")" 2>/dev/null
	GIT_DAEMON_PID=
	rm -f but_daemon_output "$GIT_DAEMON_PIDFILE"
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
