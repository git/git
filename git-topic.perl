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
	my (@args) = @_;
	my @revs;
	open(REVS, '-|', qw(git rev-list --no-merges), @args)
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

sub rebase_marker {
	my ($topic, $stage, $in_next) = @_;
	my @not_in_topic = read_revs_short('^master', "^$topic", "$stage");

	# @$in_next is what is in $stage but not in $base.
	# @not_in_topic excludes what came from $topic from @$in_next.
	# $topic can be rebased if these two set matches, because
	# no commits in $topic has been merged to $stage yet.
	if (@not_in_topic != @$in_next) {
		# we cannot rebase it anymore
		return ' ';
	}
	if (read_revs_short('master', "^$topic")) {
		# there is something that is in master but not in topic.
		return '^';
	}
	# topic is up to date.
	return '*';
}

sub describe_topic {
	my ($topic) = @_;
	open(CONF, '-|', qw(git repo-config --get),
		"branch.$topic.description")
		or die;
	my $it = join('',<CONF>);
	close(CONF);
	chomp($it);
	if ($it) {
		wrap_print("  $it");
	}
}

sub wrap_print {
	my ($string) = @_;
	format STDOUT =
~^<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
	$string
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

my @in_next = read_revs_short('^master', $stage[0]);

while (<TOPIC>) {
	chomp;
	my ($sha1, $date, $topic) = m|^([0-9a-f]{40})\s(.*?)\srefs/heads/(.+)$|
		or next;
	my @revs = read_revs($base, $sha1, (1<<@stage)-1);
	next unless (@revs || $all);

	my %revs = map { $_->[0] => $_ } @revs; # fast index
	for (my $i = 0; $i < @stage; $i++) {
		for my $item (read_revs_short("^$stage[$i]", $sha1)) {
			if (exists $revs{$item}) {
				$revs{$item}[2] &= ~(1 << $i);
			}
		}
	}

	print '*' . rebase_marker($sha1, $stage[0], \@in_next);
	print " $topic ($date)\n";
	describe_topic($topic);
	for my $item (@revs) {
		my $mark = $item->[2];
		if ($mark < @mark) {
			$mark = $mark[$mark];
		}
		wrap_print("$mark $item->[1]");
	}
}
