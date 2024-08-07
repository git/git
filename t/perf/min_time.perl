#!/usr/bin/perl

my $minrt = 1e100;
my $min;

while (<>) {
	# [h:]m:s.xx U.xx S.xx
	/^(?:(\d+):)?(\d+):(\d+(?:\.\d+)?) (\d+(?:\.\d+)?) (\d+(?:\.\d+)?)$/
		or die "bad input line: $_";
	my $rt = ((defined $1 ? $1 : 0.0)*60+$2)*60+$3;
	if ($rt < $minrt) {
		$min = $_;
		$minrt = $rt;
	}
}

if (!defined $min) {
	die "no input found";
}

print $min;
