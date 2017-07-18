#!/usr/bin/env perl

# Test our own home rolled URL canonicalizer.  Test the private one
# directly because we can't predict what the SVN API is doing to do.

use strict;
use warnings;

use Test::More 'no_plan';

use Git::SVN::Utils;
my $canonicalize_url = \&Git::SVN::Utils::_canonicalize_url_ourselves;

my %tests = (
	"http://x.com"			=> "http://x.com",
	"http://x.com/"			=> "http://x.com",
	"http://x.com/foo/bar"		=> "http://x.com/foo/bar",
	"http://x.com//foo//bar//"	=> "http://x.com/foo/bar",
	"http://x.com/  /%/"		=> "http://x.com/%20%20/%25",
);

for my $arg (keys %tests) {
	my $want = $tests{$arg};

	is $canonicalize_url->($arg), $want, "canonicalize_url('$arg') => $want";
}
