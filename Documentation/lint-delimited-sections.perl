#!/usr/bin/perl

use strict;
use warnings;

my $exit_code = 0;
sub report {
	my ($msg) = @_;
	print STDERR "$ARGV:$.: $msg\n";
	$exit_code = 1;
}

my $line_length = 0;
my $in_section = 0;
my $section_header = "";


while (my $line = <>) {
	if (($line =~ /^\+?$/) ||
	    ($line =~ /^\[.*\]$/) ||
	    ($line =~ /^ifdef::/)) {
		$line_length = 0;
	} elsif ($line =~ /^[^-.]/) {
		$line_length = length($line);
	} elsif (($line =~ /^-{3,}$/) || ($line =~ /^\.{3,}$/)) {
		if ($in_section) {
			if ($line eq $section_header) {
				$in_section = 0;
			}
		next;
		}
		if ($line_length == 0) {
			$in_section = 1;
			$section_header = $line;
			next;
		}
		if (($line_length != 0) && (length($line) != $line_length)) {
			report("section delimiter not preceded by an empty line");
		}
		$line_length = 0;
	}
}

if ($in_section) {
	report("section not finished");
}

exit $exit_code;
