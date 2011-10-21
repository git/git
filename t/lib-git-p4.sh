#
# Library code for git-p4 tests
#

. ./test-lib.sh

if ! test_have_prereq PYTHON; then
	skip_all='skipping git-p4 tests; python not available'
	test_done
fi
( p4 -h && p4d -h ) >/dev/null 2>&1 || {
	skip_all='skipping git-p4 tests; no p4 or p4d'
	test_done
}

GITP4="$GIT_BUILD_DIR/contrib/fast-import/git-p4"

# Try to pick a unique port: guess a large number, then hope
# no more than one of each test is running.
#
# This does not handle the case where somebody else is running the
# same tests and has chosen the same ports.
testid=${this_test#t}
git_p4_test_start=9800
P4DPORT=$((10669 + ($testid - $git_p4_test_start)))

export P4PORT=localhost:$P4DPORT
export P4CLIENT=client

db="$TRASH_DIRECTORY/db"
cli="$TRASH_DIRECTORY/cli"
git="$TRASH_DIRECTORY/git"
pidfile="$TRASH_DIRECTORY/p4d.pid"

start_p4d() {
	mkdir -p "$db" "$cli" "$git" &&
	(
		p4d -q -r "$db" -p $P4DPORT &
		echo $! >"$pidfile"
	) &&
	for i in 1 2 3 4 5 ; do
		p4 info >/dev/null 2>&1 && break || true &&
		echo waiting for p4d to start &&
		sleep 1
	done &&
	# complain if it never started
	p4 info >/dev/null &&
	(
		cd "$cli" &&
		p4 client -i <<-EOF
		Client: client
		Description: client
		Root: $cli
		View: //depot/... //client/...
		EOF
	)
}

kill_p4d() {
	pid=$(cat "$pidfile")
	# it had better exist for the first kill
	kill $pid &&
	for i in 1 2 3 4 5 ; do
		kill $pid >/dev/null 2>&1 || break
		sleep 1
	done &&
	# complain if it would not die
	test_must_fail kill $pid >/dev/null 2>&1 &&
	rm -rf "$db" "$cli" "$pidfile"
}

cleanup_git() {
	rm -rf "$git"
}
