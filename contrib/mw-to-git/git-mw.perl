#!/usr/bin/perl

# Copyright (C) 2013
#     Benoit Person <benoit.person@ensimag.imag.fr>
#     Celestin Matte <celestin.matte@ensimag.imag.fr>
# License: GPL v2 or later

# Set of tools for git repo with a mediawiki remote.
# Documentation & bugtracker: https://github.com/moy/Git-Mediawiki/

use strict;
use warnings;

use Getopt::Long;

# By default, use UTF-8 to communicate with Git and the user
binmode STDERR, ':encoding(UTF-8)';
binmode STDOUT, ':encoding(UTF-8)';

# Global parameters
my $verbose = 0;
sub v_print {
	if ($verbose) {
		return print {*STDERR} @_;
	}
	return;
}

my %commands = (
	'help' =>
		[\&help, {}, \&help]
);

# Search for sub-command
my $cmd = $commands{'help'};
for (0..@ARGV-1) {
	if (defined $commands{$ARGV[$_]}) {
		$cmd = $commands{$ARGV[$_]};
		splice @ARGV, $_, 1;
		last;
	}
};
GetOptions( %{$cmd->[1]},
	'help|h' => \&{$cmd->[2]},
	'verbose|v'  => \$verbose);

# Launch command
&{$cmd->[0]};

############################## Help Functions ##################################

sub help {
	print {*STDOUT} <<'END';
usage: git mw <command> <args>

git mw commands are:
    help        Display help information about git mw
END
	exit;
}
