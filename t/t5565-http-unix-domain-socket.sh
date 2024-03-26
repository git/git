#!/bin/sh

test_description="test fetching through http via unix domain socket"

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh

test -z "$NO_UNIX_SOCKETS" || {
	skip_all='skipping http-unix-socket tests, unix sockets not available'
	test_done
}

if ! test_have_prereq PERL
then
	skip_all='skipping http-unix-socket tests; perl not available'
	test_done
fi

SOCKET_PROXY_PIDFILE="$(pwd)/proxy.pid"
UDS_SOCKET="$(pwd)/uds.sock"
UNRESOLVABLE_ENDPOINT=http://unresolved

start_proxy_unix_to_tcp() {
	test_atexit 'stop_proxy_unix_to_tcp'

	perl -Mstrict -MIO::Select -MIO::Socket::INET -MIO::Socket::UNIX -e '
		my $uds_path = $ARGV[0];
		my $host = $ARGV[1];
		my $port = $ARGV[2];
		my $pidfile = $ARGV[3];

		open(my $fh, ">", $pidfile) or die "failed to create pidfile";
		print $fh "$$";
		close($fh);

		my $uds = IO::Socket::UNIX->new(
			Local => $uds_path,
			Type => SOCK_STREAM,
			Listen => 5,
		) or die "failed to create unix domain socket";

		while (my $conn = $uds->accept()) {
			my $tcp_client = IO::Socket::INET->new(
				PeerAddr => $host,
				PeerPort => $port,
				Proto => "tcp",
			) or die "failed to create TCP socket";

			my $sel = IO::Select->new($conn, $tcp_client);

			while (my @ready = $sel->can_read(10)) {
				foreach my $socket (@ready) {
					my $other = ($socket == $conn) ? $tcp_client : $conn;
					my $data;
					my $bytes = $socket->sysread($data, 4096);

					if ($bytes) {
						$other->syswrite($data, $bytes);
					} else {
						$socket->close();
					}
				}
			}
		}
	' "$UDS_SOCKET" "127.0.0.1" "$LIB_HTTPD_PORT" "$SOCKET_PROXY_PIDFILE" &
	SOCKET_PROXY_PID=$!
}

stop_proxy_unix_to_tcp() {
	kill -9 "$(cat "$SOCKET_PROXY_PIDFILE")"
	rm -f "$SOCKET_PROXY_PIDFILE"
	rm -f "$UDS_SOCKET"
}

start_httpd
start_proxy_unix_to_tcp

test_expect_success 'setup repository' '
	test_commit foo &&
	git init --bare "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push --mirror "$HTTPD_DOCUMENT_ROOT_PATH/repo.git"
'

# sanity check that we can't clone normally
test_expect_success 'cloning without UDS fails' '
	test_must_fail git clone "$UNRESOLVABLE_ENDPOINT/smart/repo.git" clone
'

test_expect_success 'cloning with UDS succeeds' '
	test_when_finished "rm -rf clone" &&
	test_config_global http.unixsocket "$UDS_SOCKET" &&
	git clone "$UNRESOLVABLE_ENDPOINT/smart/repo.git" clone
'

test_expect_success 'cloning with a non-existent http proxy fails' '
	git clone $HTTPD_URL/smart/repo.git clone &&
	rm -rf clone &&
	test_config_global http.proxy 127.0.0.1:0 &&
	test_must_fail git clone $HTTPD_URL/smart/repo.git clone
'

test_expect_success 'UDS socket takes precedence over http proxy' '
	test_when_finished "rm -rf clone" &&
	test_config_global http.proxy 127.0.0.1:0 &&
	test_config_global http.unixsocket "$UDS_SOCKET" &&
	git clone $HTTPD_URL/smart/repo.git clone
'

test_done
