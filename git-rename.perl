#!/usr/bin/perl
#
# Copyright 2005, Ryan Anderson <ryan@michonline.com>
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Linus Torvalds.


use warnings;
use strict;

sub usage($);

# Sanity checks:
my $GIT_DIR = $ENV{'GIT_DIR'} || ".git";

unless ( -d $GIT_DIR && -d $GIT_DIR . "/objects" && 
	-d $GIT_DIR . "/objects/" && -d $GIT_DIR . "/refs") {
	usage("Git repository not found.");
}

usage("") if scalar @ARGV != 2;

my ($src,$dst) = @ARGV;

unless (-f $src || -l $src || -d $src) {
	usage("git rename: bad source '$src'");
}

if (-e $dst) {
	usage("git rename: destinations '$dst' already exists");
}

my (@allfiles,@srcfiles,@dstfiles);

$/ = "\0";
open(F,"-|","git-ls-files","-z")
	or die "Failed to open pipe from git-ls-files: " . $!;

@allfiles = map { chomp; $_; } <F>;
close(F);

my $safesrc = quotemeta($src);
@srcfiles = grep /^$safesrc/, @allfiles;
@dstfiles = @srcfiles;
s#^$safesrc(/|$)#$dst$1# for @dstfiles;

rename($src,$dst)
	or die "rename failed: $!";

my $rc = system("git-update-index","--add","--",@dstfiles);
die "git-update-index failed to add new name with code $?\n" if $rc;

$rc = system("git-update-index","--remove","--",@srcfiles);
die "git-update-index failed to remove old name with code $?\n" if $rc;


sub usage($) {
	my $s = shift;
	print $s, "\n" if (length $s != 0);
	print <<EOT;
$0 <source> <dest>
source must exist and be either a file, symlink or directory.
dest must NOT exist.

Renames source to dest, and updates the git cache to reflect the change.
Use "git commit" to make record the change permanently.
EOT
	exit(1);
}
