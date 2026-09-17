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
# name and the test body with a `?!LINT: ...?!` annotation at the location of
# each detected problem, where "..." is an explanation of the problem. Returns
# zero if no problems are discovered, otherwise non-zero.

use warnings;
use strict;
use Config;
use File::Glob;
use Getopt::Long;

my $jobs = -1;
my $show_stats;
my $emit_all;

use File::Basename;
do(dirname($0) . "/lib-shell-parser.pl")
	or die "$0: failed to load lib-shell-parser.pl: $@$!\n";

# TestParser is a subclass of ShellParser which, beyond parsing shell script
# code, is also imbued with semantic knowledge of test construction, and checks
# tests for common problems (such as broken &&-chains) which might hide bugs in
# the tests themselves or in behaviors being exercised by the tests. As such,
# TestParser is only called upon to parse test bodies, not the top-level
# scripts in which the tests are defined.

package TestParser;

our @ISA = ('ShellParser');

sub new {
	my $class = shift @_;
	my $self = $class->SUPER::new(@_);
	$self->{problems} = [];
	return $self;
}

sub find_non_nl {
	my $tokens = shift @_;
	my $n = shift @_;
	$n = $#$tokens if !defined($n);
	$n-- while $n >= 0 && $$tokens[$n]->[0] eq "\n";
	return $n;
}

sub ends_with {
	my ($tokens, $needles) = @_;
	my $n = find_non_nl($tokens);
	for my $needle (reverse(@$needles)) {
		return undef if $n < 0;
		$n = find_non_nl($tokens, $n), next if $needle eq "\n";
		return undef if $$tokens[$n]->[0] !~ $needle;
		$n--;
	}
	return 1;
}

sub match_ending {
	my ($tokens, $endings) = @_;
	for my $needles (@$endings) {
		next if @$tokens < scalar(grep {$_ ne "\n"} @$needles);
		return 1 if ends_with($tokens, $needles);
	}
	return undef;
}

sub parse_loop_body {
	my $self = shift @_;
	my @tokens = $self->SUPER::parse_loop_body(@_);
	# did loop signal failure via "|| return" or "|| exit"?
	return @tokens if !@tokens || grep {$_->[0] =~ /^(?:return|exit|\$\?)$/} @tokens;
	# did loop upstream of a pipe signal failure via "|| echo 'impossible
	# text'" as the final command in the loop body?
	return @tokens if ends_with(\@tokens, [qr/^\|\|$/, "\n", qr/^echo$/, qr/^.+$/]);
	# flag missing "return/exit" handling explicit failure in loop body
	my $n = find_non_nl(\@tokens);
	push(@{$self->{problems}}, [$self->{insubshell} ? 'LOOPEXIT' : 'LOOPRETURN', $tokens[$n]]);
	return @tokens;
}

my @safe_endings = (
	[qr/^(?:&&|\|\||\||&)$/],
	[qr/^(?:exit|return)$/, qr/^(?:\d+|\$\?)$/],
	[qr/^(?:exit|return)$/, qr/^(?:\d+|\$\?)$/, qr/^;$/],
	[qr/^(?:exit|return|continue)$/],
	[qr/^(?:exit|return|continue)$/, qr/^;$/]);

sub accumulate {
	my ($self, $tokens, $cmd) = @_;
	my $problems = $self->{problems};

	# no previous command to check for missing "&&"
	goto DONE unless @$tokens;

	# new command is empty line; can't yet check if previous is missing "&&"
	goto DONE if @$cmd == 1 && $$cmd[0]->[0] eq "\n";

	# did previous command end with "&&", "|", "|| return" or similar?
	goto DONE if match_ending($tokens, \@safe_endings);

	# if this command handles "$?" specially, then okay for previous
	# command to be missing "&&"
	for my $token (@$cmd) {
		goto DONE if $token->[0] =~ /\$\?/;
	}

	# if this command is "false", "return 1", or "exit 1" (which signal
	# failure explicitly), then okay for all preceding commands to be
	# missing "&&"
	if ($$cmd[0]->[0] =~ /^(?:false|return|exit)$/) {
		@$problems = grep {$_->[0] ne 'AMP'} @$problems;
		goto DONE;
	}

	# flag missing "&&" at end of previous command
	my $n = find_non_nl($tokens);
	push(@$problems, ['AMP', $tokens->[$n]]) unless $n < 0;

DONE:
	$self->SUPER::accumulate($tokens, $cmd);
}

# ChainlintParser extends ScriptParser with &&-chain checking
package ChainlintParser;

our @ISA = ('ScriptParser');

sub format_problem {
	local $_ = shift;
	/^AMP$/ && return "missing '&&'";
	/^LOOPRETURN$/ && return "missing '|| return 1'";
	/^LOOPEXIT$/ && return "missing '|| exit 1'";
	/^HEREDOC$/ && return 'unclosed heredoc';
	die("unrecognized problem type '$_'\n");
}

sub check_test {
	my $self = shift @_;
	my $title = ScriptParser::unwrap(shift @_);
	my $body = shift @_;
	my $lineno = $body->[3];
	$body = ScriptParser::unwrap($body);
	if ($body eq '-') {
		my $herebody = shift @_;
		$body = $herebody->{content};
		$lineno = $herebody->{start_line};
	}
	$self->{ntests}++;
	my $parser = TestParser->new(\$body);
	my @tokens = $parser->parse();
	my $problems = $parser->{problems};
	$self->{nerrs} += @$problems;
	return unless $emit_all || @$problems;
	my $c = main::fd_colors(1);
	my ($erropen, $errclose) = -t 1 ? ("$c->{rev}$c->{red}", $c->{reset}) : ('?!', '?!');
	my $start = 0;
	my $checked = '';
	for (sort {$a->[1]->[2] <=> $b->[1]->[2]} @$problems) {
		my ($label, $token) = @$_;
		my $pos = $token->[2];
		my $err = format_problem($label);
		$checked .= substr($body, $start, $pos - $start);
		$checked .= ' ' unless $checked =~ /\s$/;
		$checked .= "${erropen}LINT: $err$errclose";
		$checked .= ' ' unless $pos >= length($body) ||
		    substr($body, $pos, 1) =~ /^\s/;
		$start = $pos;
	}
	$checked .= substr($body, $start);
	$checked =~ s/^/$lineno++ . ' '/mge;
	$checked =~ s/^\d+ \n//;
	$checked =~ s/^\d+/$c->{dim}$&$c->{reset}/mg;
	$checked .= "\n" unless $checked =~ /\n$/;
	push(@{$self->{output}}, "$c->{blue}# chainlint: $title$c->{reset}\n$checked");
}

# main contains high-level functionality for processing command-line switches,
# feeding input test scripts to ChainlintParser, and reporting results.
package main;

my $getnow = sub { return time(); };
my $interval = sub { return time() - shift; };
if (eval {require Time::HiRes; Time::HiRes->import(); 1;}) {
	$getnow = sub { return [Time::HiRes::gettimeofday()]; };
	$interval = sub { return Time::HiRes::tv_interval(shift); };
}

# Restore TERM if test framework set it to "dumb" so 'tput' will work; do this
# outside of get_colors() since under 'ithreads' all threads use %ENV of main
# thread and ignore %ENV changes in subthreads.
$ENV{TERM} = $ENV{USER_TERM} if $ENV{USER_TERM};

my @NOCOLORS = (bold => '', rev => '', dim => '', reset => '', blue => '', green => '', red => '');
my %COLORS = ();
sub get_colors {
	return \%COLORS if %COLORS;
	if (exists($ENV{NO_COLOR})) {
		%COLORS = @NOCOLORS;
		return \%COLORS;
	}
	if ($ENV{TERM} =~ /xterm|xterm-\d+color|xterm-new|xterm-direct|nsterm|nsterm-\d+color|nsterm-direct/) {
		%COLORS = (bold  => "\e[1m",
			   rev   => "\e[7m",
			   dim   => "\e[2m",
			   reset => "\e[0m",
			   blue  => "\e[34m",
			   green => "\e[32m",
			   red   => "\e[31m");
		return \%COLORS;
	}
	if (system("tput sgr0 >/dev/null 2>&1") == 0 &&
	    system("tput bold >/dev/null 2>&1") == 0 &&
	    system("tput rev  >/dev/null 2>&1") == 0 &&
	    system("tput dim  >/dev/null 2>&1") == 0 &&
	    system("tput setaf 1 >/dev/null 2>&1") == 0) {
		%COLORS = (bold  => `tput bold`,
			   rev   => `tput rev`,
			   dim   => `tput dim`,
			   reset => `tput sgr0`,
			   blue  => `tput setaf 4`,
			   green => `tput setaf 2`,
			   red   => `tput setaf 1`);
		return \%COLORS;
	}
	%COLORS = @NOCOLORS;
	return \%COLORS;
}

my %FD_COLORS = ();
sub fd_colors {
	my $fd = shift;
	return $FD_COLORS{$fd} if exists($FD_COLORS{$fd});
	$FD_COLORS{$fd} = -t $fd ? get_colors() : {@NOCOLORS};
	return $FD_COLORS{$fd};
}

sub ncores {
	# Windows
	if (exists($ENV{NUMBER_OF_PROCESSORS})) {
		my $ncpu = $ENV{NUMBER_OF_PROCESSORS};
		return $ncpu > 0 ? $ncpu : 1;
	}
	# Linux / MSYS2 / Cygwin / WSL
	if (open my $fh, '<', '/proc/cpuinfo') {
		my $cpuinfo = do { local $/; <$fh> };
		close($fh);
		if ($cpuinfo =~ /^n?cpus active\s*:\s*(\d+)/m) {
			return $1 if $1 > 0;
		}
		my @matches = ($cpuinfo =~ /^(processor|CPU)[\s\d]*:/mg);
		return @matches ? scalar(@matches) : 1;
	}
	# macOS & BSD
	if ($^O =~ /(?:^darwin$|bsd)/) {
		my $ncpu = qx/sysctl -n hw.ncpu/;
		return $ncpu > 0 ? $ncpu : 1;
	}
	return 1;
}

sub show_stats {
	my ($start_time, $stats) = @_;
	my $walltime = $interval->($start_time);
	my ($usertime) = times();
	my ($total_workers, $total_scripts, $total_tests, $total_errs) = (0, 0, 0, 0);
	my $c = fd_colors(2);
	print(STDERR $c->{green});
	for (@$stats) {
		my ($worker, $nscripts, $ntests, $nerrs) = @$_;
		print(STDERR "worker $worker: $nscripts scripts, $ntests tests, $nerrs errors\n");
		$total_workers++;
		$total_scripts += $nscripts;
		$total_tests += $ntests;
		$total_errs += $nerrs;
	}
	printf(STDERR "total: %d workers, %d scripts, %d tests, %d errors, %.2fs/%.2fs (wall/user)$c->{reset}\n", $total_workers, $total_scripts, $total_tests, $total_errs, $walltime, $usertime);
}

sub check_script {
	my ($id, $next_script, $emit) = @_;
	my ($nscripts, $ntests, $nerrs) = (0, 0, 0);
	while (my $path = $next_script->()) {
		$nscripts++;
		my $fh;
		unless (open($fh, "<:unix:crlf", $path)) {
			$emit->("?!ERR?! $path: $!\n");
			next;
		}
		my $s = do { local $/; <$fh> };
		close($fh);
		my $parser = ChainlintParser->new(\$s);
		1 while $parser->parse_cmd();
		if (@{$parser->{output}}) {
			my $c = fd_colors(1);
			my $s = join('', @{$parser->{output}});
			$emit->("$c->{bold}$c->{blue}# chainlint: $path$c->{reset}\n" . $s);
		}
		$ntests += $parser->{ntests};
		$nerrs += $parser->{nerrs};
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
	"jobs|j=i" => \$jobs,
	"stats|show-stats!" => \$show_stats) or die("option error\n");
$jobs = ncores() if $jobs < 1;

my $start_time = $getnow->();
my @stats;

my @scripts;
push(@scripts, File::Glob::bsd_glob($_)) for (@ARGV);
unless (@scripts) {
	show_stats($start_time, \@stats) if $show_stats;
	exit;
}
$jobs = @scripts if @scripts < $jobs;

unless ($jobs > 1 &&
	$Config{useithreads} && eval {
	require threads; threads->import();
	require Thread::Queue; Thread::Queue->import();
	1;
	}) {
	push(@stats, check_script(1, sub { shift(@scripts); }, sub { print(@_); }));
	show_stats($start_time, \@stats) if $show_stats;
	exit(exit_code(\@stats));
}

my $script_queue = Thread::Queue->new();
my $output_queue = Thread::Queue->new();

sub next_script { return $script_queue->dequeue(); }
sub emit { $output_queue->enqueue(@_); }

sub monitor {
	while (my $s = $output_queue->dequeue()) {
		print($s);
	}
}

my $mon = threads->create({'context' => 'void'}, \&monitor);
threads->create({'context' => 'list'}, \&check_script, $_, \&next_script, \&emit) for 1..$jobs;

$script_queue->enqueue(@scripts);
$script_queue->end();

for (threads->list()) {
	push(@stats, $_->join()) unless $_ == $mon;
}

$output_queue->end();
$mon->join();

show_stats($start_time, \@stats) if $show_stats;
exit(exit_code(\@stats));
