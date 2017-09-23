#!/usr/bin/perl

use lib '../../perl/blib/lib';
use strict;
use warnings;
use Git;

sub get_times {
	my $name = shift;
	open my $fh, "<", $name or return undef;
	my $line = <$fh>;
	return undef if not defined $line;
	close $fh or die "cannot close $name: $!";
	$line =~ /^(?:(\d+):)?(\d+):(\d+(?:\.\d+)?) (\d+(?:\.\d+)?) (\d+(?:\.\d+)?)$/
		or die "bad input line: $line";
	my $rt = ((defined $1 ? $1 : 0.0)*60+$2)*60+$3;
	return ($rt, $4, $5);
}

sub format_times {
	my ($r, $u, $s, $firstr) = @_;
	if (!defined $r) {
		return "<missing>";
	}
	my $out = sprintf "%.2f(%.2f+%.2f)", $r, $u, $s;
	if (defined $firstr) {
		if ($firstr > 0) {
			$out .= sprintf " %+.1f%%", 100.0*($r-$firstr)/$firstr;
		} elsif ($r == 0) {
			$out .= " =";
		} else {
			$out .= " +inf";
		}
	}
	return $out;
}

my (@dirs, %dirnames, %dirabbrevs, %prefixes, @tests);
while (scalar @ARGV) {
	my $arg = $ARGV[0];
	my $dir;
	last if -f $arg or $arg eq "--";
	if (! -d $arg) {
		my $rev = Git::command_oneline(qw(rev-parse --verify), $arg);
		$dir = "build/".$rev;
	} else {
		$arg =~ s{/*$}{};
		$dir = $arg;
		$dirabbrevs{$dir} = $dir;
	}
	push @dirs, $dir;
	$dirnames{$dir} = $arg;
	my $prefix = $dir;
	$prefix =~ tr/^a-zA-Z0-9/_/c;
	$prefixes{$dir} = $prefix . '.';
	shift @ARGV;
}

if (not @dirs) {
	@dirs = ('.');
}
$dirnames{'.'} = $dirabbrevs{'.'} = "this tree";
$prefixes{'.'} = '';

shift @ARGV if scalar @ARGV and $ARGV[0] eq "--";

@tests = @ARGV;
if (not @tests) {
	@tests = glob "p????-*.sh";
}

my $resultsdir = "test-results";
if ($ENV{GIT_PERF_SUBSECTION} ne "") {
	$resultsdir .= "/" . $ENV{GIT_PERF_SUBSECTION};
}

my @subtests;
my %shorttests;
for my $t (@tests) {
	$t =~ s{(?:.*/)?(p(\d+)-[^/]+)\.sh$}{$1} or die "bad test name: $t";
	my $n = $2;
	my $fname = "$resultsdir/$t.subtests";
	open my $fp, "<", $fname or die "cannot open $fname: $!";
	for (<$fp>) {
		chomp;
		/^(\d+)$/ or die "malformed subtest line: $_";
		push @subtests, "$t.$1";
		$shorttests{"$t.$1"} = "$n.$1";
	}
	close $fp or die "cannot close $fname: $!";
}

sub read_descr {
	my $name = shift;
	open my $fh, "<", $name or return "<error reading description>";
	binmode $fh, ":utf8" or die "PANIC on binmode: $!";
	my $line = <$fh>;
	close $fh or die "cannot close $name";
	chomp $line;
	return $line;
}

my %descrs;
my $descrlen = 4; # "Test"
for my $t (@subtests) {
	$descrs{$t} = $shorttests{$t}.": ".read_descr("$resultsdir/$t.descr");
	$descrlen = length $descrs{$t} if length $descrs{$t}>$descrlen;
}

sub have_duplicate {
	my %seen;
	for (@_) {
		return 1 if exists $seen{$_};
		$seen{$_} = 1;
	}
	return 0;
}
sub have_slash {
	for (@_) {
		return 1 if m{/};
	}
	return 0;
}

my %newdirabbrevs = %dirabbrevs;
while (!have_duplicate(values %newdirabbrevs)) {
	%dirabbrevs = %newdirabbrevs;
	last if !have_slash(values %dirabbrevs);
	%newdirabbrevs = %dirabbrevs;
	for (values %newdirabbrevs) {
		s{^[^/]*/}{};
	}
}

my %times;
my @colwidth = ((0)x@dirs);
for my $i (0..$#dirs) {
	my $d = $dirs[$i];
	my $w = length (exists $dirabbrevs{$d} ? $dirabbrevs{$d} : $dirnames{$d});
	$colwidth[$i] = $w if $w > $colwidth[$i];
}
for my $t (@subtests) {
	my $firstr;
	for my $i (0..$#dirs) {
		my $d = $dirs[$i];
		$times{$prefixes{$d}.$t} = [get_times("$resultsdir/$prefixes{$d}$t.times")];
		my ($r,$u,$s) = @{$times{$prefixes{$d}.$t}};
		my $w = length format_times($r,$u,$s,$firstr);
		$colwidth[$i] = $w if $w > $colwidth[$i];
		$firstr = $r unless defined $firstr;
	}
}
my $totalwidth = 3*@dirs+$descrlen;
$totalwidth += $_ for (@colwidth);

binmode STDOUT, ":utf8" or die "PANIC on binmode: $!";

printf "%-${descrlen}s", "Test";
for my $i (0..$#dirs) {
	my $d = $dirs[$i];
	printf "   %-$colwidth[$i]s", (exists $dirabbrevs{$d} ? $dirabbrevs{$d} : $dirnames{$d});
}
print "\n";
print "-"x$totalwidth, "\n";
for my $t (@subtests) {
	printf "%-${descrlen}s", $descrs{$t};
	my $firstr;
	for my $i (0..$#dirs) {
		my $d = $dirs[$i];
		my ($r,$u,$s) = @{$times{$prefixes{$d}.$t}};
		printf "   %-$colwidth[$i]s", format_times($r,$u,$s,$firstr);
		$firstr = $r unless defined $firstr;
	}
	print "\n";
}
