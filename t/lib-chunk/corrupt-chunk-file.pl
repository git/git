#!/usr/bin/perl

my ($chunk, $seek, $bytes) = @ARGV;
$bytes =~ s/../chr(hex($&))/ge;

binmode STDIN;
binmode STDOUT;

# A few helpers to read bytes, or read and copy them to the
# output.
sub get {
	my $n = shift;
	return unless $n;
	read(STDIN, my $buf, $n)
		or die "read error or eof: $!\n";
	return $buf;
}
sub copy {
	my $buf = get(@_);
	print $buf;
	return $buf;
}

# read until we find table-of-contents entry for chunk;
# note that we cheat a bit by assuming 4-byte alignment and
# that no ToC entry will accidentally look like a header.
#
# If we don't find the entry, copy() will hit EOF and exit
# (which should cause the caller to fail the test).
while (copy(4) ne $chunk) { }
my $offset = unpack("Q>", copy(8));

# In clear mode, our length will change. So figure out
# the length by comparing to the offset of the next chunk, and
# then adjust that offset (and all subsequent) ones.
my $len;
if ($seek eq "clear") {
	my $id;
	do {
		$id = copy(4);
		my $next = unpack("Q>", get(8));
		if (!defined $len) {
			$len = $next - $offset;
		}
		print pack("Q>", $next - $len + length($bytes));
	} while (unpack("N", $id));
}

# and now copy up to our existing chunk data
copy($offset - tell(STDIN));
if ($seek eq "clear") {
	# if clearing, skip past existing data
	get($len);
} else {
	# otherwise, copy up to the requested offset,
	# and skip past the overwritten bytes
	copy($seek);
	get(length($bytes));
}

# now write out the requested bytes, along
# with any other remaining data
print $bytes;
while (read(STDIN, my $buf, 4096)) {
	print $buf;
}
