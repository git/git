#!/usr/bin/perl -w
#
# Copyright (c) 2006 Junio C Hamano
#

use strict;
use Getopt::Long;

my $topic_pattern = '??*/*';
my $base = 'next';
my @stage = qw(next pu);
my @mark = ('.', '?', '-', '+');
my $all = 0;
my $merges = 0;
my $tests = 0;

my @custom_stage;
my @custom_mark;
GetOptions("topic=s" => \$topic_pattern,
	   "base=s" => \$base,
	   "stage=s" => \@custom_stage,
	   "mark=s" => \@custom_mark,
	   "merges!" => \$merges,
	   "tests!" => \$tests,
	   "all!" => \$all)
    or die;

if (@custom_stage) { @stage = @custom_stage; }
if (@custom_mark) { @mark = @custom_mark; }
my @nomerges = $merges ? qw(--no-merges) : ();

sub read_revs_short {
	my (@args) = @_;
	my @revs;
	open(REVS, '-|', qw(git rev-list), @nomerges, @args)
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
	open(REVS, '-|', qw(git rev-list --pretty=oneline), @nomerges,
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

my %atlog_next = ();
my %atlog_test = ();

sub next_marker {
	my ($topic) = @_;
	return '' if (!$tests);
	return '??' if (!exists $atlog_next{$topic});
	for ($atlog_next{$topic}) {
		my ($merge, $test) = ('*', '*');
		if (/rerere ok/) {
			$merge = 'R';
		} elsif (/conflict (\d+)/) {
			if ($1 < 10) {
				$merge = $1;
			} else {
				$merge = 'X';
			}
		}
		$test = 'X' if (/test error/);
		return "$merge$test";
	}
}

sub test_marker {
	my ($commit) = @_;
	return '' if (!$tests);
	my $tree = `git rev-parse "$commit^{tree}"`;
	chomp($tree);
	return "?" if (!exists $atlog_test{$tree});
	for ($atlog_test{$tree}) {
		if (/build error/) {
			return 'B';
		} elsif (/test error/) {
			return 'X';
		} else {
			return ' ';
		}
	}
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

my @in_next = read_revs_short('^master', $stage[0]);
my @topic = ();

my @topic_pattern = map { "refs/heads/$_" } (@ARGV ? @ARGV : $topic_pattern);

open(TOPIC, '-|', qw(git for-each-ref),
    '--sort=-authordate',
    '--format=%(objectname) %(authordate) %(refname)',
    @topic_pattern)
    or die;

while (<TOPIC>) {
	chomp;
	my ($sha1, $date, $topic) = m|^([0-9a-f]{40})\s(.*?)\srefs/heads/(.+)$|
	    or next;
	push @topic, [$sha1, $date, $topic];
}
close(TOPIC);

if (open(AT, "Meta/AT.log")) {
	my $next = `git rev-parse --verify refs/heads/next`;
	chomp $next;
	while (<AT>) {
		if (/^N (.{40}) (.{40})	(.*)$/ && $1 eq $next) {
			$atlog_next{$2} = $3;
			next;
		}
		if (/^A (.{40})	(.*)/) {
			$atlog_test{$1} = $2;
			next;
		}
	}
	close(AT);
}

my @last_merge_to_next = ();

for (@topic) {
	my ($sha1, $date, $topic) = @$_;
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

	print '*' .
	    next_marker($sha1) .
	    rebase_marker($sha1, $stage[0], \@in_next);
	my $count = "";
	if (1 < @revs) {
		$count = " " . (scalar @revs) . " commits";
	}
	elsif (@revs) {
		$count = " 1 commit";
	}
	print " $topic ($date)$count\n";
	describe_topic($topic);
	for my $item (@revs) {
		my $mark = $item->[2];
		if ($mark < @mark) {
			$mark = $mark[$mark];
		}
		if ($tests) {
			$mark = test_marker($item->[0]) . $mark;
		}
		wrap_print("$mark $item->[1]");
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
