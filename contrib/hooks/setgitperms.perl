#!/usr/bin/perl
#
# Copyright (c) 2006 Josh England
#
# This script can be used to save/restore full permissions and ownership data
# within a but working tree.
#
# To save permissions/ownership data, place this script in your .but/hooks
# directory and enable a `pre-cummit` hook with the following lines:
#      #!/bin/sh
#     SUBDIRECTORY_OK=1 . but-sh-setup
#     $GIT_DIR/hooks/setbutperms.perl -r
#
# To restore permissions/ownership data, place this script in your .but/hooks
# directory and enable a `post-merge` and `post-checkout` hook with the
# following lines:
#      #!/bin/sh
#     SUBDIRECTORY_OK=1 . but-sh-setup
#     $GIT_DIR/hooks/setbutperms.perl -w
#
use strict;
use Getopt::Long;
use File::Find;
use File::Basename;

my $usage =
"usage: setbutperms.perl [OPTION]... <--read|--write>
This program uses a file `.butmeta` to store/restore permissions and uid/gid
info for all files/dirs tracked by but in the repository.

---------------------------------Read Mode-------------------------------------
-r,  --read         Reads perms/etc from working dir into a .butmeta file
-s,  --stdout       Output to stdout instead of .butmeta
-d,  --diff         Show unified diff of perms file (XOR with --stdout)

---------------------------------Write Mode------------------------------------
-w,  --write        Modify perms/etc in working dir to match the .butmeta file
-v,  --verbose      Be verbose

\n";

my ($stdout, $showdiff, $verbose, $read_mode, $write_mode);

if ((@ARGV < 0) || !GetOptions(
			       "stdout",         \$stdout,
			       "diff",           \$showdiff,
			       "read",           \$read_mode,
			       "write",          \$write_mode,
			       "verbose",        \$verbose,
			      )) { die $usage; }
die $usage unless ($read_mode xor $write_mode);

my $topdir = `but rev-parse --show-cdup` or die "\n"; chomp $topdir;
my $butdir = $topdir . '.but';
my $butmeta = $topdir . '.butmeta';

if ($write_mode) {
    # Update the working dir permissions/ownership based on data from .butmeta
    open (IN, "<$butmeta") or die "Could not open $butmeta for reading: $!\n";
    while (defined ($_ = <IN>)) {
	chomp;
	if (/^(.*)  mode=(\S+)\s+uid=(\d+)\s+gid=(\d+)/) {
	    # Compare recorded perms to actual perms in the working dir
	    my ($path, $mode, $uid, $gid) = ($1, $2, $3, $4);
	    my $fullpath = $topdir . $path;
	    my (undef,undef,$wmode,undef,$wuid,$wgid) = lstat($fullpath);
	    $wmode = sprintf "%04o", $wmode & 07777;
	    if ($mode ne $wmode) {
		$verbose && print "Updating permissions on $path: old=$wmode, new=$mode\n";
		chmod oct($mode), $fullpath;
	    }
	    if ($uid != $wuid || $gid != $wgid) {
		if ($verbose) {
		    # Print out user/group names instead of uid/gid
		    my $pwname  = getpwuid($uid);
		    my $grpname  = getgrgid($gid);
		    my $wpwname  = getpwuid($wuid);
		    my $wgrpname  = getgrgid($wgid);
		    $pwname = $uid if !defined $pwname;
		    $grpname = $gid if !defined $grpname;
		    $wpwname = $wuid if !defined $wpwname;
		    $wgrpname = $wgid if !defined $wgrpname;

		    print "Updating uid/gid on $path: old=$wpwname/$wgrpname, new=$pwname/$grpname\n";
		}
		chown $uid, $gid, $fullpath;
	    }
	}
	else {
	    warn "Invalid input format in $butmeta:\n\t$_\n";
	}
    }
    close IN;
}
elsif ($read_mode) {
    # Handle merge conflicts in the .butperms file
    if (-e "$butdir/MERGE_MSG") {
	if (`grep ====== $butmeta`) {
	    # Conflict not resolved -- abort the cummit
	    print "PERMISSIONS/OWNERSHIP CONFLICT\n";
	    print "    Resolve the conflict in the $butmeta file and then run\n";
	    print "    `.but/hooks/setbutperms.perl --write` to reconcile.\n";
	    exit 1;
	}
	elsif (`grep $butmeta $butdir/MERGE_MSG`) {
	    # A conflict in .butmeta has been manually resolved. Verify that
	    # the working dir perms matches the current .butmeta perms for
	    # each file/dir that conflicted.
	    # This is here because a `setbutperms.perl --write` was not
	    # performed due to a merge conflict, so permissions/ownership
	    # may not be consistent with the manually merged .butmeta file.
	    my @conflict_diff = `but show \$(cat $butdir/MERGE_HEAD)`;
	    my @conflict_files;
	    my $metadiff = 0;

	    # Build a list of files that conflicted from the .butmeta diff
	    foreach my $line (@conflict_diff) {
		if ($line =~ m|^diff --but a/$butmeta b/$butmeta|) {
		    $metadiff = 1;
		}
		elsif ($line =~ /^diff --but/) {
		    $metadiff = 0;
		}
		elsif ($metadiff && $line =~ /^\+(.*)  mode=/) {
		    push @conflict_files, $1;
		}
	    }

	    # Verify that each conflict file now has permissions consistent
	    # with the .butmeta file
	    foreach my $file (@conflict_files) {
		my $absfile = $topdir . $file;
		my $gm_entry = `grep "^$file  mode=" $butmeta`;
		if ($gm_entry =~ /mode=(\d+)  uid=(\d+)  gid=(\d+)/) {
		    my ($gm_mode, $gm_uid, $gm_gid) = ($1, $2, $3);
		    my (undef,undef,$mode,undef,$uid,$gid) = lstat("$absfile");
		    $mode = sprintf("%04o", $mode & 07777);
		    if (($gm_mode ne $mode) || ($gm_uid != $uid)
			|| ($gm_gid != $gid)) {
			print "PERMISSIONS/OWNERSHIP CONFLICT\n";
			print "    Mismatch found for file: $file\n";
			print "    Run `.but/hooks/setbutperms.perl --write` to reconcile.\n";
			exit 1;
		    }
		}
		else {
		    print "Warning! Permissions/ownership no longer being tracked for file: $file\n";
		}
	    }
	}
    }

    # No merge conflicts -- write out perms/ownership data to .butmeta file
    unless ($stdout) {
	open (OUT, ">$butmeta.tmp") or die "Could not open $butmeta.tmp for writing: $!\n";
    }

    my @files = `but ls-files`;
    my %dirs;

    foreach my $path (@files) {
	chomp $path;
	# We have to manually add stats for parent directories
	my $parent = dirname($path);
	while (!exists $dirs{$parent}) {
	    $dirs{$parent} = 1;
	    next if $parent eq '.';
	    printstats($parent);
	    $parent = dirname($parent);
	}
	# Now the but-tracked file
	printstats($path);
    }

    # diff the temporary metadata file to see if anything has changed
    # If no metadata has changed, don't overwrite the real file
    # This is just so `but cummit -a` doesn't try to cummit a bogus update
    unless ($stdout) {
	if (! -e $butmeta) {
	    rename "$butmeta.tmp", $butmeta;
	}
	else {
	    my $diff = `diff -U 0 $butmeta $butmeta.tmp`;
	    if ($diff ne '') {
		rename "$butmeta.tmp", $butmeta;
	    }
	    else {
		unlink "$butmeta.tmp";
	    }
	    if ($showdiff) {
		print $diff;
	    }
	}
	close OUT;
    }
    # Make sure the .butmeta file is tracked
    system("but add $butmeta");
}


sub printstats {
    my $path = $_[0];
    $path =~ s/@/\@/g;
    my (undef,undef,$mode,undef,$uid,$gid) = lstat($path);
    $path =~ s/%/\%/g;
    if ($stdout) {
	print $path;
	printf "  mode=%04o  uid=$uid  gid=$gid\n", $mode & 07777;
    }
    else {
	print OUT $path;
	printf OUT "  mode=%04o  uid=$uid  gid=$gid\n", $mode & 07777;
    }
}
