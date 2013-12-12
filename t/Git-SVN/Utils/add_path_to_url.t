#!/usr/bin/env perl

use strict;
use warnings;

use Test::More 'no_plan';

use Git::SVN::Utils qw(
	add_path_to_url
);

# A reference cannot be a hash key, so we use an array.
my @tests = (
	["http://x.com", "bar"]			=> 'http://x.com/bar',
	["http://x.com", ""]			=> 'http://x.com',
	["http://x.com/foo/", undef]		=> 'http://x.com/foo/',
	["http://x.com/foo/", "/bar/baz/"]	=> 'http://x.com/foo/bar/baz/',
	["http://x.com", 'per%cent']		=> 'http://x.com/per%25cent',
);

while(@tests) {
	my($have, $want) = splice @tests, 0, 2;

	my $args = join ", ", map { qq['$_'] } map { defined($_) ? $_ : 'undef' } @$have;
	my $name = "add_path_to_url($args) eq $want";
	is add_path_to_url(@$have), $want, $name;
}
