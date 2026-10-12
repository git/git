#!/usr/bin/perl
#
# Generate a fast-import stream for p5412 connectivity check benchmarks.
#
# Usage: generate-repo-p5412-connectivity-check.perl
#            <dirs> <files_per_dir> <commits> [<hot_dirs>] [<files_per_commit>]
#
# Creates one initial commit with dirs*files_per_dir files, then
# <commits> additional commits each modifying <files_per_commit>
# files in directories chosen round-robin from 1..<hot_dirs>.

use strict;
use warnings;

my ($nd, $nf, $nc, $hot, $fpc) = @ARGV;
$hot = $nd if !$hot || $hot > $nd;
$fpc = 1   if !$fpc;

sub data {
	printf "data %d\n%s\n", length($_[0]), $_[0];
}

# Initial tree: one commit with nd*nf files.
printf "commit refs/heads/main\n";
printf "committer perf <perf\@test.com> now\n";
data("initial");
for my $d (1..$nd) {
	for my $f (1..$nf) {
		printf "M 100644 inline d-%04d/f-%03d\n", $d, $f;
		data(sprintf "%03d%03d", $d, $f);
	}
}

# Subsequent commits (auto-chained by fast-import).
for my $i (1..$nc) {
	printf "commit refs/heads/main\n";
	printf "committer perf <perf\@test.com> now\n";
	data(sprintf "change-%03d", $i);
	for my $j (0..$fpc-1) {
		my $d = (($i + $j) % $hot) + 1;
		printf "M 100644 inline d-%04d/f-001\n", $d;
		data(sprintf "c%d-%d", $i, $j);
	}
}
