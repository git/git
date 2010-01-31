#!/usr/bin/env perl
# Copyright 2005, Ryan Anderson <ryan@michonline.com>
# Distribution permitted under the GPL v2, as distributed
# by the Free Software Foundation.
# Later versions of the GPL at the discretion of Linus Torvalds
#
# Scan two git object-trees, and hardlink any common objects between them.

use 5.006;
use strict;
use warnings;
use Getopt::Long;

sub get_canonical_form($);
sub do_scan_directory($$$);
sub compare_two_files($$);
sub usage();
sub link_two_files($$);

# stats
my $total_linked = 0;
my $total_already = 0;
my ($linked,$already);

my $fail_on_different_sizes = 0;
my $help = 0;
GetOptions("safe" => \$fail_on_different_sizes,
	   "help" => \$help);

usage() if $help;

my (@dirs) = @ARGV;

usage() if (!defined $dirs[0] || !defined $dirs[1]);

$_ = get_canonical_form($_) foreach (@dirs);

my $master_dir = pop @dirs;

opendir(D,$master_dir . "objects/")
	or die "Failed to open $master_dir/objects/ : $!";

my @hashdirs = grep { ($_ eq 'pack') || /^[0-9a-f]{2}$/ } readdir(D);

foreach my $repo (@dirs) {
	$linked = 0;
	$already = 0;
	printf("Searching '%s' and '%s' for common objects and hardlinking them...\n",
		$master_dir,$repo);

	foreach my $hashdir (@hashdirs) {
		do_scan_directory($master_dir, $hashdir, $repo);
	}

	printf("Linked %d files, %d were already linked.\n",$linked, $already);

	$total_linked += $linked;
	$total_already += $already;
}

printf("Totals: Linked %d files, %d were already linked.\n",
	$total_linked, $total_already);


sub do_scan_directory($$$) {
	my ($srcdir, $subdir, $dstdir) = @_;

	my $sfulldir = sprintf("%sobjects/%s/",$srcdir,$subdir);
	my $dfulldir = sprintf("%sobjects/%s/",$dstdir,$subdir);

	opendir(S,$sfulldir)
		or die "Failed to opendir $sfulldir: $!";

	foreach my $file (grep(!/\.{1,2}$/, readdir(S))) {
		my $sfilename = $sfulldir . $file;
		my $dfilename = $dfulldir . $file;

		compare_two_files($sfilename,$dfilename);

	}
	closedir(S);
}

sub compare_two_files($$) {
	my ($sfilename, $dfilename) = @_;

	# Perl's stat returns relevant information as follows:
	# 0 = dev number
	# 1 = inode number
	# 7 = size
	my @sstatinfo = stat($sfilename);
	my @dstatinfo = stat($dfilename);

	if (@sstatinfo == 0 && @dstatinfo == 0) {
		die sprintf("Stat of both %s and %s failed: %s\n",$sfilename, $dfilename, $!);

	} elsif (@dstatinfo == 0) {
		return;
	}

	if ( ($sstatinfo[0] == $dstatinfo[0]) &&
	     ($sstatinfo[1] != $dstatinfo[1])) {
		if ($sstatinfo[7] == $dstatinfo[7]) {
			link_two_files($sfilename, $dfilename);

		} else {
			my $err = sprintf("ERROR: File sizes are not the same, cannot relink %s to %s.\n",
				$sfilename, $dfilename);
			if ($fail_on_different_sizes) {
				die $err;
			} else {
				warn $err;
			}
		}

	} elsif ( ($sstatinfo[0] == $dstatinfo[0]) &&
	     ($sstatinfo[1] == $dstatinfo[1])) {
		$already++;
	}
}

sub get_canonical_form($) {
	my $dir = shift;
	my $original = $dir;

	die "$dir is not a directory." unless -d $dir;

	$dir .= "/" unless $dir =~ m#/$#;
	$dir .= ".git/" unless $dir =~ m#\.git/$#;

	die "$original does not have a .git/ subdirectory.\n" unless -d $dir;

	return $dir;
}

sub link_two_files($$) {
	my ($sfilename, $dfilename) = @_;
	my $tmpdname = sprintf("%s.old",$dfilename);
	rename($dfilename,$tmpdname)
		or die sprintf("Failure renaming %s to %s: %s",
			$dfilename, $tmpdname, $!);

	if (! link($sfilename,$dfilename)) {
		my $failtxt = "";
		unless (rename($tmpdname,$dfilename)) {
			$failtxt = sprintf(
				"Git Repository containing %s is probably corrupted, " .
				"please copy '%s' to '%s' to fix.\n",
				$tmpdname, $dfilename);
		}

		die sprintf("Failed to link %s to %s: %s\n%s" .
			$sfilename, $dfilename,
			$!, $dfilename, $failtxt);
	}

	unlink($tmpdname)
		or die sprintf("Unlink of %s failed: %s\n",
			$dfilename, $!);

	$linked++;
}


sub usage() {
	print("Usage: git relink [--safe] <dir> [<dir> ...] <master_dir> \n");
	print("All directories should contain a .git/objects/ subdirectory.\n");
	print("Options\n");
	print("\t--safe\t" .
		"Stops if two objects with the same hash exist but " .
		"have different sizes.  Default is to warn and continue.\n");
	exit(1);
}
