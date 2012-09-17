#
# Library code for git p4 tests
#

# p4 tests never use the top-level repo; always build/clone into
# a subdirectory called "$git"
TEST_NO_CREATE_REPO=NoThanks

. ./test-lib.sh

if ! test_have_prereq PYTHON; then
	skip_all='skipping git p4 tests; python not available'
	test_done
fi
( p4 -h && p4d -h ) >/dev/null 2>&1 || {
	skip_all='skipping git p4 tests; no p4 or p4d'
	test_done
}

# Try to pick a unique port: guess a large number, then hope
# no more than one of each test is running.
#
# This does not handle the case where somebody else is running the
# same tests and has chosen the same ports.
testid=${this_test#t}
git_p4_test_start=9800
P4DPORT=$((10669 + ($testid - $git_p4_test_start)))

P4PORT=localhost:$P4DPORT
P4CLIENT=client
P4EDITOR=:
export P4PORT P4CLIENT P4EDITOR

db="$TRASH_DIRECTORY/db"
cli=$(test-path-utils real_path "$TRASH_DIRECTORY/cli")
git="$TRASH_DIRECTORY/git"
pidfile="$TRASH_DIRECTORY/p4d.pid"

start_p4d() {
	mkdir -p "$db" "$cli" "$git" &&
	rm -f "$pidfile" &&
	(
		p4d -q -r "$db" -p $P4DPORT &
		echo $! >"$pidfile"
	) &&

	# This gives p4d a long time to start up, as it can be
	# quite slow depending on the machine.  Set this environment
	# variable to something smaller to fail faster in, say,
	# an automated test setup.  If the p4d process dies, that
	# will be caught with the "kill -0" check below.
	i=${P4D_START_PATIENCE:-300}
	pid=$(cat "$pidfile")
	ready=
	while test $i -gt 0
	do
		# succeed when p4 client commands start to work
		if p4 info >/dev/null 2>&1
		then
			ready=true
			break
		fi
		# fail if p4d died
		kill -0 $pid 2>/dev/null || break
		echo waiting for p4d to start
		sleep 1
		i=$(( $i - 1 ))
	done

	if test -z "$ready"
	then
		# p4d failed to start
		return 1
	fi

	# build a client
	(
		cd "$cli" &&
		p4 client -i <<-EOF
		Client: client
		Description: client
		Root: $cli
		View: //depot/... //client/...
		EOF
	)
	return 0
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
	rm -rf "$git" &&
	mkdir "$git"
}

marshal_dump() {
	what=$1 &&
	line=${2:-1} &&
	cat >"$TRASH_DIRECTORY/marshal-dump.py" <<-EOF &&
	import marshal
	import sys
	for i in range($line):
	    d = marshal.load(sys.stdin)
	print d['$what']
	EOF
	"$PYTHON_PATH" "$TRASH_DIRECTORY/marshal-dump.py"
}

#
# Construct a client with this list of View lines
#
client_view() {
	(
		cat <<-EOF &&
		Client: client
		Description: client
		Root: $cli
		View:
		EOF
		for arg ; do
			printf "\t$arg\n"
		done
	) | p4 client -i
}
