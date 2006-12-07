#!/usr/bin/perl -w
#
# Copyright (c) 2006 Junio C Hamano
#

use strict;
use Getopt::Long;

my $topic_pattern = '??/*';
my $base = 'next';
my @stage = qw(next pu);
my @mark = ('.', '?', '-', '+');
my $all = 0;

my @custom_stage;
my @custom_mark;
GetOptions("topic=s" => \$topic_pattern,
	   "base=s" => \$base,
	   "stage=s" => \@custom_stage,
	   "mark=s" => \@custom_mark,
	   "all!" => \$all)
    or die;

if (@custom_stage) { @stage = @custom_stage; }
if (@custom_mark) { @mark = @custom_mark; }

sub read_revs_short {
	my ($bottom, $top) = @_;
	my @revs;
	open(REVS, '-|', qw(git rev-list --no-merges), "$bottom..$top")
	    or die;
	while (<REVS>) {
		chomp;
		push @revs, $_;
	}
	close(REVS);
	return @revs;
}

sub read_revs {
	my ($bottom, $top, $mask) = @_;
	my @revs;
	open(REVS, '-|', qw(git rev-list --pretty=oneline --no-merges),
	     "$bottom..$top")
	    or die;
	while (<REVS>) {
		chomp;
		my ($sha1, $topic) = /^([0-9a-f]{40}) (.*)$/;
		push @revs, [$sha1, $topic, $mask];
	}
	close(REVS);
	return @revs;
}

sub wrap_print {
	my ($prefix, $string) = @_;
	format STDOUT =
  @ ^<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
	$prefix, $string
  ~~^<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
	$string
.
	write;
}

open(TOPIC, '-|', qw(git for-each-ref),
	'--sort=-authordate',
	'--format=%(objectname) %(authordate) %(refname)',
	"refs/heads/$topic_pattern")
	or die;

while (<TOPIC>) {
	chomp;
	my ($sha1, $date, $topic) = m|^([0-9a-f]{40})\s(.*?)\srefs/heads/(.+)$|
		or next;
	my @revs = read_revs($base, $sha1, (1<<@stage)-1);
	next unless (@revs || $all);

	my %revs = map { $_->[0] => $_ } @revs; # fast index
	for (my $i = 0; $i < @stage; $i++) {
		for my $item (read_revs_short($stage[$i], $sha1)) {
			if (exists $revs{$item}) {
				$revs{$item}[2] &= ~(1 << $i);
			}
		}
	}
	print "* $topic ($date)\n";
	for my $item (@revs) {
		my $mark = $item->[2];
		if ($mark < @mark) {
			$mark = $mark[$mark];
		}
		wrap_print($mark, $item->[1]);
	}
}
