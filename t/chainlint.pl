#!/usr/bin/env perl
#
# Copyright (c) 2021-2022 Eric Sunshine <sunshine@sunshineco.com>
#
# This tool scans shell scripts for test definitions and checks those tests for
# problems, such as broken &&-chains, which might hide bugs in the tests
# themselves or in behaviors being exercised by the tests.
#
# Input arguments are pathnames of shell scripts containing test definitions,
# or globs referencing a collection of scripts. For each problem discovered,
# the pathname of the script containing the test is printed along with the test
# name and the test body with a `?!FOO?!` annotation at the location of each
# detected problem, where "FOO" is a tag such as "AMP" which indicates a broken
# &&-chain. Returns zero if no problems are discovered, otherwise non-zero.

use warnings;
use strict;
use File::Glob;
use Getopt::Long;

my $show_stats;
my $emit_all;

package ScriptParser;

sub new {
	my $class = shift @_;
	my $self = bless {} => $class;
	$self->{output} = [];
	$self->{ntests} = 0;
	return $self;
}

sub parse_cmd {
	return undef;
}

# main contains high-level functionality for processing command-line switches,
# feeding input test scripts to ScriptParser, and reporting results.
package main;

my $getnow = sub { return time(); };
my $interval = sub { return time() - shift; };
if (eval {require Time::HiRes; Time::HiRes->import(); 1;}) {
	$getnow = sub { return [Time::HiRes::gettimeofday()]; };
	$interval = sub { return Time::HiRes::tv_interval(shift); };
}

sub show_stats {
	my ($start_time, $stats) = @_;
	my $walltime = $interval->($start_time);
	my ($usertime) = times();
	my ($total_workers, $total_scripts, $total_tests, $total_errs) = (0, 0, 0, 0);
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		print(STDERR "worker $worker: $nscripts scripts, $ntests tests, $nerrs errors\n");
		$total_workers++;
		$total_scripts += $nscripts;
		$total_tests += $ntests;
		$total_errs += $nerrs;
	}
	printf(STDERR "total: %d workers, %d scripts, %d tests, %d errors, %.2fs/%.2fs (wall/user)\n", $total_workers, $total_scripts, $total_tests, $total_errs, $walltime, $usertime);
}

sub check_script {
	my ($id, $next_script, $emit) = @_;
	my ($nscripts, $ntests, $nerrs) = (0, 0, 0);
	while (my $path = $next_script->()) {
		$nscripts++;
		my $fh;
		unless (open($fh, "<", $path)) {
			$emit->("?!ERR?! $path: $!\n");
			next;
		}
		my $s = do { local $/; <$fh> };
		close($fh);
		my $parser = ScriptParser->new(\$s);
		1 while $parser->parse_cmd();
		if (@{$parser->{output}}) {
			my $s = join('', @{$parser->{output}});
			$emit->("# chainlint: $path\n" . $s);
			$nerrs += () = $s =~ /\?![^?]+\?!/g;
		}
		$ntests += $parser->{ntests};
	}
	return [$id, $nscripts, $ntests, $nerrs];
}

sub exit_code {
	my $stats = shift @_;
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		return 1 if $nerrs;
	}
	return 0;
}

Getopt::Long::Configure(qw{bundling});
GetOptions(
	"emit-all!" => \$emit_all,
	"stats|show-stats!" => \$show_stats) or die("option error\n");

my $start_time = $getnow->();
my @stats;

my @scripts;
push(@scripts, File::Glob::bsd_glob($_)) for (@ARGV);
unless (@scripts) {
	show_stats($start_time, \@stats) if $show_stats;
	exit;
}

push(@stats, check_script(1, sub { shift(@scripts); }, sub { print(@_); }));
show_stats($start_time, \@stats) if $show_stats;
exit(exit_code(\@stats));
