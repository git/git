#!/usr/bin/env perl

use strict;
use warnings;

# Assemble expected output for check-greplint target.
# Usage: greplint-cat.pl <outdir> <test-name> ...
#
# For each <test-name>, reads greplint/<test-name>.expect and
# prepends "greplint/<test-name>.test:" to every non-empty line,
# matching the output format of greplint.pl.  Writes combined
# expected output to <outdir>/expect.

my $outdir = shift;
open(my $expect, '>', "$outdir/expect")
	or die "unable to open $outdir/expect: $!";

for my $name (@ARGV) {
	open(my $fh, '<', "greplint/$name.expect")
		or die "unable to open greplint/$name.expect: $!";
	while (<$fh>) {
		print $expect "greplint/$name.test:$_";
	}
	close $fh;
}

close $expect;
