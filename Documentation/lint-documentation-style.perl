#!/usr/bin/perl

use strict;
use warnings;

my $exit_code = 0;
sub report {
	my ($line, $msg) = @_;
	chomp $line;
	print STDERR "$ARGV:$.: '$line' $msg\n";
	$exit_code = 1;
}

my $synopsis_style = 0;

while (my $line = <>) {
	if ($line =~ /^[ \t]*`?[-a-z0-9.]+`?(, `?[-a-z0-9.]+`?)+(::|;;)$/) {

		report($line, "multiple parameters in a definition list item");
	}
	if ($line =~ /^`?--\[no-\][a-z0-9-]+.*(::|;;)$/) {
		report($line, "definition list item with a `--[no-]` parameter");
	}
	if ($line =~ /^\[synopsis\]$/) {
		$synopsis_style = 1;
	}
	if (($line =~ /^(-[-a-z].*|<[-a-z0-9]+>(\.{3})?)(::|;;)$/) && ($synopsis_style)) {
			report($line, "synopsis style and definition list item not backquoted");
	}
}


exit $exit_code;
