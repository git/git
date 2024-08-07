#!/usr/bin/env perl

use strict;
use warnings;

use Test::More 'no_plan';

use Git::SVN::Utils;
my $collapse_dotdot = \&Git::SVN::Utils::_collapse_dotdot;

my %tests = (
	"foo/bar/baz"			=> "foo/bar/baz",
	".."				=> "..",
	"foo/.."			=> "",
	"/foo/bar/../../baz"		=> "/baz",
	"deeply/.././deeply/nested"	=> "./deeply/nested",
);

for my $arg (keys %tests) {
	my $want = $tests{$arg};

	is $collapse_dotdot->($arg), $want, "_collapse_dotdot('$arg') => $want";
}
