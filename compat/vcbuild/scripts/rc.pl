#!/usr/bin/perl -w
######################################################################
# Compile Resources on Windows
#
# This is a wrapper to facilitate the compilation of Git with MSVC
# using GNU Make as the build system. So, instead of manipulating the
# Makefile into something nasty, just to support non-space arguments
# etc, we use this wrapper to fix the command line options
#
######################################################################
use strict;
my @args = ();
my @input = ();

while (@ARGV) {
	my $arg = shift @ARGV;
	if ("$arg" =~ /^-[dD]/) {
		# GIT_VERSION gets passed with too many
		# layers of dquote escaping.
		$arg =~ s/\\"/"/g;

		push(@args, $arg);

	} elsif ("$arg" eq "-i") {
		my $arg = shift @ARGV;
		# TODO complain if NULL or is dashed ??
		push(@input, $arg);

	} elsif ("$arg" eq "-o") {
		my $arg = shift @ARGV;
		# TODO complain if NULL or is dashed ??
		push(@args, "-fo$arg");

	} else {
		push(@args, $arg);
	}
}

push(@args, "-nologo");
push(@args, "-v");
push(@args, @input);

unshift(@args, "rc.exe");
printf("**** @args\n");

exit (system(@args) != 0);
