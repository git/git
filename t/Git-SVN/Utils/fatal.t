#!/usr/bin/perl

use strict;
use warnings;

use Test::More 'no_plan';

BEGIN {
	# Override exit at BEGIN time before Git::SVN::Utils is loaded
	# so it will see our local exit later.
	*CORE::GLOBAL::exit = sub(;$) {
	return @_ ? CORE::exit($_[0]) : CORE::exit();
	};
}

use Git::SVN::Utils qw(fatal);

# fatal()
{
	# Capture the exit code and prevent exit.
	my $exit_status;
	no warnings 'redefine';
	local *CORE::GLOBAL::exit = sub { $exit_status = $_[0] || 0 };

	# Trap fatal's message to STDERR
	my $stderr;
	close STDERR;
	ok open STDERR, ">", \$stderr;

	fatal "Some", "Stuff", "Happened";

	is $stderr, "Some Stuff Happened\n";
	is $exit_status, 1;
}
