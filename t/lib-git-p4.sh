#
# Library code for git p4 tests
#

# p4 tests never use the top-level repo; always build/clone into
# a subdirectory called "$git"
TEST_NO_CREATE_REPO=NoThanks

# Some operations require multiple attempts to be successful. Define
# here the maximal retry timeout in seconds.
RETRY_TIMEOUT=60

# Sometimes p4d seems to hang. Terminate the p4d process automatically after
# the defined timeout in seconds.
P4D_TIMEOUT=300

. ./test-lib.sh

if ! test_have_prereq PYTHON
then
	skip_all='skipping git p4 tests; python not available'
	test_done
fi
( p4 -h && p4d -h ) >/dev/null 2>&1 || {
	skip_all='skipping git p4 tests; no p4 or p4d'
	test_done
}

# On cygwin, the NT version of Perforce can be used.  When giving
# it paths, either on the command-line or in client specifications,
# be sure to use the native windows form.
#
# Older versions of perforce were available compiled natively for
# cygwin.  Those do not accept native windows paths, so make sure
# not to convert for them.
native_path () {
	path="$1" &&
	if test_have_prereq CYGWIN && ! p4 -V | grep -q CYGWIN
	then
		path=$(cygpath --windows "$path")
	else
		path=$(test-tool path-utils real_path "$path")
	fi &&
	echo "$path"
}

test_set_port P4DPORT

P4PORT=localhost:$P4DPORT
P4CLIENT=client
P4USER=author
P4EDITOR=true
unset P4CHARSET
export P4PORT P4CLIENT P4USER P4EDITOR P4CHARSET

db="$TRASH_DIRECTORY/db"
cli="$TRASH_DIRECTORY/cli"
git="$TRASH_DIRECTORY/git"
pidfile="$TRASH_DIRECTORY/p4d.pid"

stop_p4d_and_watchdog () {
	kill -9 $p4d_pid $watchdog_pid
}

# git p4 submit generates a temp file, which will
# not get cleaned up if the submission fails.  Don't
# clutter up /tmp on the test machine.
TMPDIR="$TRASH_DIRECTORY"
export TMPDIR

registered_stop_p4d_atexit_handler=
start_p4d () {
	# One of the test scripts stops and then re-starts p4d.
	# Don't register and then run the same atexit handlers several times.
	if test -z "$registered_stop_p4d_atexit_handler"
	then
		test_atexit 'stop_p4d_and_watchdog'
		registered_stop_p4d_atexit_handler=AlreadyDone
	fi

	mkdir -p "$db" "$cli" "$git" &&
	rm -f "$pidfile" &&
	(
		cd "$db" &&
		{
			p4d -q -p $P4DPORT "$@" &
			echo $! >"$pidfile"
		}
	) &&
	p4d_pid=$(cat "$pidfile")

	# This gives p4d a long time to start up, as it can be
	# quite slow depending on the machine.  Set this environment
	# variable to something smaller to fail faster in, say,
	# an automated test setup.  If the p4d process dies, that
	# will be caught with the "kill -0" check below.
	i=${P4D_START_PATIENCE:-300}

	nr_tries_left=$P4D_TIMEOUT
	while true
	do
		if test $nr_tries_left -eq 0
		then
			kill -9 $p4d_pid
			exit 1
		fi
		sleep 1
		nr_tries_left=$(($nr_tries_left - 1))
	done 2>/dev/null 4>&2 &
	watchdog_pid=$!

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
		kill -0 $p4d_pid 2>/dev/null || break
		echo waiting for p4d to start
		sleep 1
		i=$(( $i - 1 ))
	done

	if test -z "$ready"
	then
		# p4d failed to start
		return 1
	fi

	# build a p4 user so author@example.com has an entry
	p4_add_user author

	# build a client
	client_view "//depot/... //client/..." &&

	return 0
}

p4_add_user () {
	name=$1 &&
	p4 user -f -i <<-EOF
	User: $name
	Email: $name@example.com
	FullName: Dr. $name
	EOF
}

p4_add_job () {
	p4 job -f -i <<-EOF
	Job: $1
	Status: open
	User: dummy
	Description:
	EOF
}

retry_until_success () {
	nr_tries_left=$RETRY_TIMEOUT
	until "$@" 2>/dev/null || test $nr_tries_left -eq 0
	do
		sleep 1
		nr_tries_left=$(($nr_tries_left - 1))
	done
}

stop_and_cleanup_p4d () {
	kill -9 $p4d_pid $watchdog_pid
	wait $p4d_pid
	rm -rf "$db" "$cli" "$pidfile"
}

cleanup_git () {
	retry_until_success rm -r "$git"
	test_path_is_missing "$git" &&
	retry_until_success mkdir "$git"
}

marshal_dump () {
	what=$1 &&
	line=${2:-1} &&
	cat >"$TRASH_DIRECTORY/marshal-dump.py" <<-EOF &&
	import marshal
	import sys
	instream = getattr(sys.stdin, 'buffer', sys.stdin)
	for i in range($line):
	    d = marshal.load(instream)
	print(d[b'$what'].decode('utf-8'))
	EOF
	"$PYTHON_PATH" "$TRASH_DIRECTORY/marshal-dump.py"
}

#
# Construct a client with this list of View lines
#
client_view () {
	(
		cat <<-EOF &&
		Client: $P4CLIENT
		Description: $P4CLIENT
		Root: $cli
		AltRoots: $(native_path "$cli")
		LineEnd: unix
		View:
		EOF
		printf "\t%s\n" "$@"
	) | p4 client -i
}

is_cli_file_writeable () {
	# cygwin version of p4 does not set read-only attr,
	# will be marked 444 but -w is true
	file="$1" &&
	if test_have_prereq CYGWIN && p4 -V | grep -q CYGWIN
	then
		stat=$(stat --format=%a "$file") &&
		test $stat = 644
	else
		test -w "$file"
	fi
}
