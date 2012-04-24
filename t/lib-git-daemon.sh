#!/bin/sh

if test -z "$GIT_TEST_GIT_DAEMON"
then
	skip_all="git-daemon testing disabled (define GIT_TEST_GIT_DAEMON to enable)"
	test_done
fi

LIB_GIT_DAEMON_PORT=${LIB_GIT_DAEMON_PORT-'8121'}

GIT_DAEMON_PID=
GIT_DAEMON_DOCUMENT_ROOT_PATH="$PWD"/repo
GIT_DAEMON_URL=git://127.0.0.1:$LIB_GIT_DAEMON_PORT

start_git_daemon() {
	if test -n "$GIT_DAEMON_PID"
	then
		error "start_git_daemon already called"
	fi

	mkdir -p "$GIT_DAEMON_DOCUMENT_ROOT_PATH"

	trap 'code=$?; stop_git_daemon; (exit $code); die' EXIT

	say >&3 "Starting git daemon ..."
	test-git-daemon --listen=127.0.0.1 --port="$LIB_GIT_DAEMON_PORT" \
		--reuseaddr --verbose \
		--base-path="$GIT_DAEMON_DOCUMENT_ROOT_PATH" \
		"$@" "$GIT_DAEMON_DOCUMENT_ROOT_PATH" \
		>&3 2>&4 ||
		error "git daemon failed to start"
	GIT_DAEMON_PID=$(cat git-daemon.pid)
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
	GIT_DAEMON_PID=
}
