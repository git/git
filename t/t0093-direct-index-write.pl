#!/usr/bin/perl
#
# Build a v2 index file from entries listed on stdin.
# Each line: "octalmode hex-oid name"
# Output: binary index written to stdout.
#
# This bypasses all D/F safety checks in add_index_entry(), simulating
# what happens when code uses ADD_CACHE_JUST_APPEND to bulk-load entries.
use strict;
use warnings;
use Digest::SHA qw(sha1 sha256);

my $hash_algo = $ENV{'GIT_DEFAULT_HASH'} || 'sha1';
my $hash_func = $hash_algo eq 'sha256' ? \&sha256 : \&sha1;

my @entries;
while (my $line = <STDIN>) {
	chomp $line;
	my ($mode, $oid_hex, $name) = split(/ /, $line, 3);
	push @entries, [$mode, $oid_hex, $name];
}

my $body = "DIRC" . pack("NN", 2, scalar @entries);

for my $ent (@entries) {
	my ($mode, $oid_hex, $name) = @{$ent};
	# 10 x 32-bit stat fields (zeroed), with mode in position 7
	my $stat = pack("N10", 0, 0, 0, 0, 0, 0, oct($mode), 0, 0, 0);
	my $oid = pack("H*", $oid_hex);
	my $flags = pack("n", length($name) & 0xFFF);
	my $entry = $stat . $oid . $flags . $name . "\0";
	# Pad to 8-byte boundary
	while (length($entry) % 8) { $entry .= "\0"; }
	$body .= $entry;
}

binmode STDOUT;
print $body . $hash_func->($body);
