#!/usr/bin/perl -w
######################################################################
# Compiles or links files
#
# This is a wrapper to facilitate the compilation of Git with MSVC
# using GNU Make as the build system. So, instead of manipulating the
# Makefile into something nasty, just to support non-space arguments
# etc, we use this wrapper to fix the command line options
#
# Copyright (C) 2009 Marius Storm-Olsen <mstormo@gmail.com>
######################################################################
use strict;
my @args = ();
my @cflags = ();
my $is_linking = 0;
while (@ARGV) {
	my $arg = shift @ARGV;
	if ("$arg" =~ /^-[DIMGO]/) {
		push(@cflags, $arg);
	} elsif ("$arg" eq "-o") {
		my $file_out = shift @ARGV;
		if ("$file_out" =~ /exe$/) {
			$is_linking = 1;
			push(@args, "-OUT:$file_out");
		} else {
			push(@args, "-Fo$file_out");
		}
	} elsif ("$arg" eq "-lz") {
		push(@args, "zlib.lib");
	} elsif ("$arg" eq "-liconv") {
		push(@args, "iconv.lib");
	} elsif ("$arg" eq "-lcrypto") {
		push(@args, "libeay32.lib");
	} elsif ("$arg" eq "-lssl") {
		push(@args, "ssleay32.lib");
	} elsif ("$arg" =~ /^-L/ && "$arg" ne "-LTCG") {
		$arg =~ s/^-L/-LIBPATH:/;
		push(@args, $arg);
	} elsif ("$arg" =~ /^-R/) {
		# eat
	} else {
		push(@args, $arg);
	}
}
if ($is_linking) {
	unshift(@args, "link.exe");
} else {
	unshift(@args, "cl.exe");
	push(@args, @cflags);
}
#printf("**** @args\n");
exit (system(@args) != 0);
