use strict;
use IO::Select;
use IO::Socket::UNIX;
use IO::Socket::INET;

my $path = shift;

unlink($path);
my $server = IO::Socket::UNIX->new(Listen => 1, Local => $path)
	or die "unable to listen on $path: $!";

$| = 1;
print "ready\n";

while (my $client = $server->accept()) {
	sysread $client, my $buf, 8;
	my ($version, $cmd, $port, $ip) = unpack 'CCnN', $buf;
	next unless $version == 4; # socks4
	next unless $cmd == 1; # TCP stream connection

	# skip NUL-terminated id
	while (sysread $client, my $char, 1) {
		last unless ord($char);
	}

	# version(0), reply(5a == granted), port (ignored), ip (ignored)
	syswrite $client, "\x00\x5a\x00\x00\x00\x00\x00\x00";

	my $remote = IO::Socket::INET->new(PeerHost => $ip, PeerPort => $port)
		or die "unable to connect to $ip/$port: $!";

	my $io = IO::Select->new($client, $remote);
	while ($io->count) {
		for my $fh ($io->can_read(0)) {
			for my $pair ([$client, $remote], [$remote, $client]) {
				my ($from, $to) = @$pair;
				next unless $fh == $from;

				my $r = sysread $from, my $buf, 1024;
				if (!defined $r || $r <= 0) {
					$io->remove($from);
					next;
				}
				syswrite $to, $buf;
			}
		}
	}
}
