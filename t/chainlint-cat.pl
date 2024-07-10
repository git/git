#!/usr/bin/env perl

my $outdir = shift;
open(my $tests, '>', "$outdir/tests")
	or die "unable to open $outdir/tests: $!";
open(my $expect, '>', "$outdir/expect")
	or die "unable to open $outdir/expect: $!";

print $expect "# chainlint: $outdir/tests\n";

my $offset = 0;
for my $script (@ARGV) {
	print $expect "# chainlint: $script\n";

	open(my $expect_in, '<', "chainlint/$script.expect")
		or die "unable to open chainlint/$script.expect: $!";
	while (<$expect_in>) {
		s/^\d+/$& + $offset/e;
		print $expect $_;
	}

	open(my $test_in, '<', "chainlint/$script.test")
		or die "unable to open chainlint/$script.test: $!";
	while (<$test_in>) {
		/^# LINT: / and next;
		print $tests $_;
		$offset++;
	}
}
