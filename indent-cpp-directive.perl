#!/usr/bin/perl

use strict;
use warnings;

my $indent_level = -1;

sub emit {
	my $indent = $indent_level <= 0 ? "" : " " x $indent_level;
	printf "#%s%s", $indent, $_;
}

while (<>) {
	unless (s/^\s*#\s*//) {
		print;
		next;
	}

	if (/^if/) {
		emit($_);
		$indent_level++;
	} elsif (/^el/) {
		$indent_level--;
		emit($_);
		$indent_level++;
	} elsif (/^endif/) {
		$indent_level--;
		emit($_);
	} else {
		emit($_);
	}
}
