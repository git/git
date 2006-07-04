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
use Git;

sub usage() {
	print <<EOT;
$0 [-f] [-n] <source> <destination>
$0 [-f] [-n] [-k] <source> ... <destination directory>
EOT
	exit(1);
}

our ($opt_n, $opt_f, $opt_h, $opt_k, $opt_v);
getopts("hnfkv") || usage;
usage() if $opt_h;
@ARGV >= 1 or usage;

my $repo = Git->repository();

my (@srcArgs, @dstArgs, @srcs, @dsts);
my ($src, $dst, $base, $dstDir);

# remove any trailing slash in arguments
for (@ARGV) { s/\/*$//; }

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
    if ($argCount < 2) {
	print "Error: need at least two arguments\n";
	exit(1);
    }
    if ($argCount > 2) {
	print "Error: moving to directory '"
	    . $ARGV[$argCount-1]
	    . "' not possible; not existing\n";
	exit(1);
    }
    @srcArgs = ($ARGV[0]);
    @dstArgs = ($ARGV[1]);
    $dstDir = "";
}

my $subdir_prefix = $repo->wc_subdir();

# run in git base directory, so that git-ls-files lists all revisioned files
chdir $repo->wc_path();
$repo->wc_chdir('');

# normalize paths, needed to compare against versioned files and update-index
# also, this is nicer to end-users by doing ".//a/./b/.//./c" ==> "a/b/c"
for (@srcArgs, @dstArgs) {
    # prepend git prefix as we run from base directory
    $_ = $subdir_prefix.$_;
    s|^\./||;
    s|/\./|/| while (m|/\./|);
    s|//+|/|g;
    # Also "a/b/../c" ==> "a/c"
    1 while (s,(^|/)[^/]+/\.\./,$1,);
}

my (@allfiles,@srcfiles,@dstfiles);
my $safesrc;
my (%overwritten, %srcForDst);

{
	local $/ = "\0";
	@allfiles = $repo->command('ls-files', '-z');
}


my ($i, $bad);
while(scalar @srcArgs > 0) {
    $src = shift @srcArgs;
    $dst = shift @dstArgs;
    $bad = "";

    for ($src, $dst) {
	# Be nicer to end-users by doing ".//a/./b/.//./c" ==> "a/b/c"
	s|^\./||;
	s|/\./|/| while (m|/\./|);
	s|//+|/|g;
	# Also "a/b/../c" ==> "a/c"
	1 while (s,(^|/)[^/]+/\.\./,$1,);
    }

    if ($opt_v) {
	print "Checking rename of '$src' to '$dst'\n";
    }

    unless (-f $src || -l $src || -d $src) {
	$bad = "bad source '$src'";
    }

    $safesrc = quotemeta($src);
    @srcfiles = grep /^$safesrc(\/|$)/, @allfiles;

    $overwritten{$dst} = 0;
    if (($bad eq "") && -e $dst) {
	$bad = "destination '$dst' already exists";
	if ($opt_f) {
	    # only files can overwrite each other: check both source and destination
	    if (-f $dst && (scalar @srcfiles == 1)) {
		print "Warning: $bad; will overwrite!\n";
		$bad = "";
		$overwritten{$dst} = 1;
	    }
	    else {
		$bad = "Can not overwrite '$src' with '$dst'";
	    }
	}
    }
    
    if (($bad eq "") && ($dst =~ /^$safesrc\//)) {
	$bad = "can not move directory '$src' into itself";
    }

    if ($bad eq "") {
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
	exit(1);
    }
    push @srcs, $src;
    push @dsts, $dst;
}

# Final pass: rename/move
my (@deletedfiles,@addedfiles,@changedfiles);
$bad = "";
while(scalar @srcs > 0) {
    $src = shift @srcs;
    $dst = shift @dsts;

    if ($opt_n || $opt_v) { print "Renaming $src to $dst\n"; }
    if (!$opt_n) {
	if (!rename($src,$dst)) {
	    $bad = "renaming '$src' failed: $!";
	    if ($opt_k) {
		print "Warning: skipped: $bad\n";
		$bad = "";
		next;
	    }
	    last;
	}
    }

    $safesrc = quotemeta($src);
    @srcfiles = grep /^$safesrc(\/|$)/, @allfiles;
    @dstfiles = @srcfiles;
    s/^$safesrc(\/|$)/$dst$1/ for @dstfiles;

    push @deletedfiles, @srcfiles;
    if (scalar @srcfiles == 1) {
	# $dst can be a directory with 1 file inside
	if ($overwritten{$dst} ==1) {
	    push @changedfiles, $dstfiles[0];

	} else {
	    push @addedfiles, $dstfiles[0];
	}
    }
    else {
	push @addedfiles, @dstfiles;
    }
}

if ($opt_n) {
    if (@changedfiles) {
	print "Changed  : ". join(", ", @changedfiles) ."\n";
    }
    if (@addedfiles) {
	print "Adding   : ". join(", ", @addedfiles) ."\n";
    }
    if (@deletedfiles) {
	print "Deleting : ". join(", ", @deletedfiles) ."\n";
    }
}
else {
    if (@changedfiles) {
	my ($fd, $ctx) = $repo->command_input_pipe('update-index', '-z', '--stdin');
	foreach my $fileName (@changedfiles) {
		print $fd "$fileName\0";
	}
	git_cmd_try { $repo->command_close_pipe($fd, $ctx); }
		'git-update-index failed to update changed files with code %d';
    }
    if (@addedfiles) {
	my ($fd, $ctx) = $repo->command_input_pipe('update-index', '--add', '-z', '--stdin');
	foreach my $fileName (@addedfiles) {
		print $fd "$fileName\0";
	}
	git_cmd_try { $repo->command_close_pipe($fd, $ctx); }
		'git-update-index failed to add new files with code %d';
    }
    if (@deletedfiles) {
	my ($fd, $ctx) = $repo->command_input_pipe('update-index', '--remove', '-z', '--stdin');
	foreach my $fileName (@deletedfiles) {
		print $fd "$fileName\0";
	}
	git_cmd_try { $repo->command_close_pipe($fd, $ctx); }
		'git-update-index failed to remove old files with code %d';
    }
}

if ($bad ne "") {
    print "Error: $bad\n";
    exit(1);
}
