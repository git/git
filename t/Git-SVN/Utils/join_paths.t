#!/usr/bin/env perl

use strict;
use warnings;

use Test::More 'no_plan';

use Git::SVN::Utils qw(
	join_paths
);

# A reference cannot be a hash key, so we use an array.
my @tests = (
	[]					=> '',
	["/x.com", "bar"]			=> '/x.com/bar',
	["x.com", ""]				=> 'x.com',
	["/x.com/foo/", undef, "bar"]		=> '/x.com/foo/bar',
	["x.com/foo/", "/bar/baz/"]		=> 'x.com/foo/bar/baz/',
	["foo", "bar"]				=> 'foo/bar',
	["/foo/bar", "baz", "/biff"]		=> '/foo/bar/baz/biff',
	["", undef, "."]			=> '.',
	[]					=> '',

);

while(@tests) {
	my($have, $want) = splice @tests, 0, 2;

	my $args = join ", ", map { qq['$_'] } map { defined($_) ? $_ : 'undef' } @$have;
	my $name = "join_paths($args) eq '$want'";
	is join_paths(@$have), $want, $name;
}
