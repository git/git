#!/usr/bin/perl

# Test t0000..t9999.sh for non portable shell scripts
# This script can be called with one or more filenames as parameters

use strict;
use warnings;

my $exit_code=0;
my %func;

sub err {
	my $msg = shift;
	s/^\s+//;
	s/\s+$//;
	s/\s+/ /g;
	print "$ARGV:$.: error: $msg: $_\n";
	$exit_code = 1;
}

# glean names of shell functions
for my $i (@ARGV) {
	open(my $f, '<', $i) or die "$0: $i: $!\n";
	while (<$f>) {
		$func{$1} = 1 if /^\s*(\w+)\s*\(\)\s*{\s*$/;
	}
	close $f;
}

my $line = '';
while (<>) {
	chomp;
	$line .= $_;
	# stitch together incomplete lines (those ending with "\")
	next if $line =~ s/\\$//;

	$_ = $line;
	/\bcp\s+-a/ and err 'cp -a is not portable';
	/\bsed\s+-[^efn]\s+/ and err 'sed option not portable (use only -n, -e, -f)';
	/\becho\s+-[neE]/ and err 'echo with option is not portable (use printf)';
	/^\s*declare\s+/ and err 'arrays/declare not portable';
	/^\s*[^#]\s*which\s/ and err 'which is not portable (use type)';
	/\btest\s+[^=]*==/ and err '"test a == b" is not portable (use =)';
	/\bwc -l.*"\s*=/ and err '`"$(wc -l)"` is not portable (use test_line_count)';
	/\bhead\s+-c\b/ and err 'head -c is not portable (use test_copy_bytes BYTES <file >out)';
	/(?:\$\(seq|^\s*seq\b)/ and err 'seq is not portable (use test_seq)';
	/\bgrep\b.*--file\b/ and err 'grep --file FILE is not portable (use grep -f FILE)';
	/\b[ef]grep\b/ and err 'egrep/fgrep obsolescent (use grep -E/-F)';
	/\bexport\s+[A-Za-z0-9_]*=/ and err '"export FOO=bar" is not portable (use FOO=bar && export FOO)';
	/\blocal\s+[A-Za-z0-9_]*=\$([A-Za-z0-9_{]|[(][^(])/ and
		err q(quote "$val" in 'local var=$val');
	/^\s*([A-Z0-9_]+=(\w*|(["']).*?\3)\s+)+(\w+)/ and exists($func{$4}) and
		err '"FOO=bar shell_func" assignment extends beyond "shell_func"';
	$line = '';
	# this resets our $. for each file
	close ARGV if eof;
}
exit $exit_code;
