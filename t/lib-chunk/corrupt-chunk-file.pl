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

# Some platforms' perl builds don't support 64-bit integers, and hence do not
# allow packing/unpacking quadwords with "Q". The chunk format uses 64-bit file
# offsets to support files of any size, but in practice our test suite will
# only use small files. So we can fake it by asking for two 32-bit values and
# discarding the first (most significant) one, which is equivalent as long as
# it's just zero.
sub unpack_quad {
	my $bytes = shift;
	my ($n1, $n2) = unpack("NN", $bytes);
	die "quad value exceeds 32 bits" if $n1;
	return $n2;
}
sub pack_quad {
	my $n = shift;
	my $ret = pack("NN", 0, $n);
	# double check that our original $n did not exceed the 32-bit limit.
	# This is presumably impossible on a 32-bit system (which would have
	# truncated much earlier), but would still alert us on a 64-bit build
	# of a new test that would fail on a 32-bit build (though we'd
	# presumably see the die() from unpack_quad() in such a case).
	die "quad round-trip failed" if unpack_quad($ret) != $n;
	return $ret;
}

# read until we find table-of-contents entry for chunk;
# note that we cheat a bit by assuming 4-byte alignment and
# that no ToC entry will accidentally look like a header.
#
# If we don't find the entry, copy() will hit EOF and exit
# (which should cause the caller to fail the test).
while (copy(4) ne $chunk) { }
my $offset = unpack_quad(copy(8));

# In clear mode, our length will change. So figure out
# the length by comparing to the offset of the next chunk, and
# then adjust that offset (and all subsequent) ones.
my $len;
if ($seek eq "clear") {
	my $id;
	do {
		$id = copy(4);
		my $next = unpack_quad(get(8));
		if (!defined $len) {
			$len = $next - $offset;
		}
		print pack_quad($next - $len + length($bytes));
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
