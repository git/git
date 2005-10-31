#!/usr/bin/perl
#
# Copyright 2005, Ryan Anderson <ryan@michonline.com>
#                 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
#
# This file is licensed under the GPL v2, or a later version
# at the discretion of Linus Torvalds.


use warnings;
use strict;
use Getopt::Std;

sub usage() {
	print <<EOT;
$0 [-f] [-n] <source> <dest>
$0 [-f] [-k] [-n] <source> ... <dest directory>

In the first form, source must exist and be either a file,
symlink or directory, dest must not exist. It renames source to dest.
In the second form, the last argument has to be an existing
directory; the given sources will be moved into this directory.

Updates the git cache to reflect the change.
Use "git commit" to make the change permanently.

Options:
  -f   Force renaming/moving, even if target exists
  -k   Continue on error by skipping
       not-existing or not revision-controlled source
  -n   Do nothing; show what would happen
EOT
	exit(1);
}

# Sanity checks:
my $GIT_DIR = $ENV{'GIT_DIR'} || ".git";

unless ( -d $GIT_DIR && -d $GIT_DIR . "/objects" && 
	-d $GIT_DIR . "/objects/" && -d $GIT_DIR . "/refs") {
    print "Git repository not found.";
    usage();
}


our ($opt_n, $opt_f, $opt_h, $opt_k, $opt_v);
getopts("hnfkv") || usage;
usage() if $opt_h;
@ARGV >= 1 or usage;

my (@srcArgs, @dstArgs, @srcs, @dsts);
my ($src, $dst, $base, $dstDir);

my $argCount = scalar @ARGV;
if (-d $ARGV[$argCount-1]) {
	$dstDir = $ARGV[$argCount-1];
	@srcArgs = @ARGV[0..$argCount-2];
	
	foreach $src (@srcArgs) {
		$base = $src;
		$base =~ s/^.*\///;
		$dst = "$dstDir/". $base;
		push @dstArgs, $dst;
	}
}
else {
    if ($argCount != 2) {
	print "Error: moving to directory '"
	    . $ARGV[$argCount-1]
	    . "' not possible; not exisiting\n";
	usage;
    }
    @srcArgs = ($ARGV[0]);
    @dstArgs = ($ARGV[1]);
    $dstDir = "";
}

my (@allfiles,@srcfiles,@dstfiles);
my $safesrc;
my (%overwritten, %srcForDst);

$/ = "\0";
open(F,"-|","git-ls-files","-z")
        or die "Failed to open pipe from git-ls-files: " . $!;

@allfiles = map { chomp; $_; } <F>;
close(F);


my ($i, $bad);
while(scalar @srcArgs > 0) {
    $src = shift @srcArgs;
    $dst = shift @dstArgs;
    $bad = "";

    if ($opt_v) {
	print "Checking rename of '$src' to '$dst'\n";
    }

    unless (-f $src || -l $src || -d $src) {
	$bad = "bad source '$src'";
    }

    $overwritten{$dst} = 0;
    if (($bad eq "") && -e $dst) {
	$bad = "destination '$dst' already exists";
	if (-f $dst && $opt_f) {
	    print "Warning: $bad; will overwrite!\n";
	    $bad = "";
	    $overwritten{$dst} = 1;
	}
    }
    
    if (($bad eq "") && ($src eq $dstDir)) {
	$bad = "can not move directory '$src' into itself";
    }

    if ($bad eq "") {
	$safesrc = quotemeta($src);
	@srcfiles = grep /^$safesrc(\/|$)/, @allfiles;
        if (scalar @srcfiles == 0) {
	    $bad = "'$src' not under version control";
	}
    }

    if ($bad eq "") {
       if (defined $srcForDst{$dst}) {
           $bad = "can not move '$src' to '$dst'; already target of ";
           $bad .= "'".$srcForDst{$dst}."'";
       }
       else {
           $srcForDst{$dst} = $src;
       }
    }

    if ($bad ne "") {
	if ($opt_k) {
	    print "Warning: $bad; skipping\n";
	    next;
	}
	print "Error: $bad\n";
	usage();
    }
    push @srcs, $src;
    push @dsts, $dst;
}

# Final pass: rename/move
my (@deletedfiles,@addedfiles,@changedfiles);
while(scalar @srcs > 0) {
    $src = shift @srcs;
    $dst = shift @dsts;

    if ($opt_n || $opt_v) { print "Renaming $src to $dst\n"; }
    if (!$opt_n) {
	rename($src,$dst)
	    or die "rename failed: $!";
    }

    $safesrc = quotemeta($src);
    @srcfiles = grep /^$safesrc(\/|$)/, @allfiles;
    @dstfiles = @srcfiles;
    s/^$safesrc(\/|$)/$dst$1/ for @dstfiles;

    push @deletedfiles, @srcfiles;
    if (scalar @srcfiles == 1) {
	if ($overwritten{$dst} ==1) {
	    push @changedfiles, $dst;
	} else {
	    push @addedfiles, $dst;
	}
    }
    else {
	push @addedfiles, @dstfiles;
    }
}

if ($opt_n) {
	print "Changed  : ". join(", ", @changedfiles) ."\n";
	print "Adding   : ". join(", ", @addedfiles) ."\n";
	print "Deleting : ". join(", ", @deletedfiles) ."\n";
	exit(1);
}
	
my $rc;
if (scalar @changedfiles >0) {
	$rc = system("git-update-index","--",@changedfiles);
	die "git-update-index failed to update changed files with code $?\n" if $rc;
}
if (scalar @addedfiles >0) {
	$rc = system("git-update-index","--add","--",@addedfiles);
	die "git-update-index failed to add new names with code $?\n" if $rc;
}
$rc = system("git-update-index","--remove","--",@deletedfiles);
die "git-update-index failed to remove old names with code $?\n" if $rc;
