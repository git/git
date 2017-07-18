#!/usr/bin/perl

# Test t0000..t9999.sh for non portable shell scripts
# This script can be called with one or more filenames as parameters

use strict;
use warnings;

my $exit_code=0;

sub err {
	my $msg = shift;
	print "$ARGV:$.: error: $msg: $_\n";
	$exit_code = 1;
}

while (<>) {
	chomp;
	/\bsed\s+-i/ and err 'sed -i is not portable';
	/\becho\s+-n/ and err 'echo -n is not portable (please use printf)';
	/^\s*declare\s+/ and err 'arrays/declare not portable';
	/^\s*[^#]\s*which\s/ and err 'which is not portable (please use type)';
	/\btest\s+[^=]*==/ and err '"test a == b" is not portable (please use =)';
	/\bexport\s+[A-Za-z0-9_]*=/ and err '"export FOO=bar" is not portable (please use FOO=bar && export FOO)';
	# this resets our $. for each file
	close ARGV if eof;
}
exit $exit_code;
