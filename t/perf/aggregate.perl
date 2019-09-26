#!/usr/bin/perl

use lib '../../perl/build/lib';
use strict;
use warnings;
use Getopt::Long;
use Git;
use Cwd qw(realpath);

sub get_times {
	my $name = shift;
	open my $fh, "<", $name or return undef;
	my $line = <$fh>;
	return undef if not defined $line;
	close $fh or die "cannot close $name: $!";
	# times
	if ($line =~ /^(?:(\d+):)?(\d+):(\d+(?:\.\d+)?) (\d+(?:\.\d+)?) (\d+(?:\.\d+)?)$/) {
		my $rt = ((defined $1 ? $1 : 0.0)*60+$2)*60+$3;
		return ($rt, $4, $5);
	# size
	} elsif ($line =~ /^\d+$/) {
		return $&;
	} else {
		die "bad input line: $line";
	}
}

sub relative_change {
	my ($r, $firstr) = @_;
	if ($firstr > 0) {
		return sprintf "%+.1f%%", 100.0*($r-$firstr)/$firstr;
	} elsif ($r == 0) {
		return "=";
	} else {
		return "+inf";
	}
}

sub format_times {
	my ($r, $u, $s, $firstr) = @_;
	# no value means we did not finish the test
	if (!defined $r) {
		return "<missing>";
	}
	# a single value means we have a size, not times
	if (!defined $u) {
		return format_size($r, $firstr);
	}
	# otherwise, we have real/user/system times
	my $out = sprintf "%.2f(%.2f+%.2f)", $r, $u, $s;
	$out .= ' ' . relative_change($r, $firstr) if defined $firstr;
	return $out;
}

sub usage {
	print <<EOT;
./aggregate.perl [options] [--] [<dir_or_rev>...] [--] [<test_script>...] >

  Options:
    --codespeed          * Format output for Codespeed
    --reponame    <str>  * Send given reponame to codespeed
    --sort-by     <str>  * Sort output (only "regression" criteria is supported)
    --subsection  <str>  * Use results from given subsection

EOT
	exit(1);
}

sub human_size {
	my $n = shift;
	my @units = ('', qw(K M G));
	while ($n > 900 && @units > 1) {
		$n /= 1000;
		shift @units;
	}
	return $n unless length $units[0];
	return sprintf '%.1f%s', $n, $units[0];
}

sub format_size {
	my ($size, $first) = @_;
	# match the width of a time: 0.00(0.00+0.00)
	my $out = sprintf '%15s', human_size($size);
	$out .= ' ' . relative_change($size, $first) if defined $first;
	return $out;
}

my (@dirs, %dirnames, %dirabbrevs, %prefixes, @tests,
    $codespeed, $sortby, $subsection, $reponame);

Getopt::Long::Configure qw/ require_order /;

my $rc = GetOptions("codespeed"     => \$codespeed,
		    "reponame=s"    => \$reponame,
		    "sort-by=s"     => \$sortby,
		    "subsection=s"  => \$subsection);
usage() unless $rc;

while (scalar @ARGV) {
	my $arg = $ARGV[0];
	my $dir;
	my $prefix = '';
	last if -f $arg or $arg eq "--";
	if (! -d $arg) {
		my $rev = Git::command_oneline(qw(rev-parse --verify), $arg);
		$dir = "build/".$rev;
	} elsif ($arg eq '.') {
		$dir = '.';
	} else {
		$dir = realpath($arg);
		$dirnames{$dir} = $dir;
		$prefix .= 'bindir';
	}
	push @dirs, $dir;
	$dirnames{$dir} ||= $arg;
	$prefix .= $dir;
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

if (! $subsection and
    exists $ENV{GIT_PERF_SUBSECTION} and
    $ENV{GIT_PERF_SUBSECTION} ne "") {
	$subsection = $ENV{GIT_PERF_SUBSECTION};
}

if ($subsection) {
	$resultsdir .= "/" . $subsection;
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

sub display_dir {
	my ($d) = @_;
	return exists $dirabbrevs{$d} ? $dirabbrevs{$d} : $dirnames{$d};
}

sub print_default_results {
	my %descrs;
	my $descrlen = 4; # "Test"
	for my $t (@subtests) {
		$descrs{$t} = $shorttests{$t}.": ".read_descr("$resultsdir/$t.descr");
		$descrlen = length $descrs{$t} if length $descrs{$t}>$descrlen;
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
		my $w = length display_dir($dirs[$i]);
		$colwidth[$i] = $w if $w > $colwidth[$i];
	}
	for my $t (@subtests) {
		my $firstr;
		for my $i (0..$#dirs) {
			my $d = $dirs[$i];
			my $base = "$resultsdir/$prefixes{$d}$t";
			$times{$prefixes{$d}.$t} = [];
			foreach my $type (qw(times size)) {
				if (-e "$base.$type") {
					$times{$prefixes{$d}.$t} = [get_times("$base.$type")];
					last;
				}
			}
			my ($r,$u,$s) = @{$times{$prefixes{$d}.$t}};
			my $w = length format_times($r,$u,$s,$firstr);
			$colwidth[$i] = $w if $w > $colwidth[$i];
			$firstr = $r unless defined $firstr;
		}
	}
	my $totalwidth = 3*@dirs+$descrlen;
	$totalwidth += $_ for (@colwidth);

	printf "%-${descrlen}s", "Test";
	for my $i (0..$#dirs) {
		printf "   %-$colwidth[$i]s", display_dir($dirs[$i]);
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
}

sub print_sorted_results {
	my ($sortby) = @_;

	if ($sortby ne "regression") {
		print "Only 'regression' is supported as '--sort-by' argument\n";
		usage();
	}

	my @evolutions;
	for my $t (@subtests) {
		my ($prevr, $prevu, $prevs, $prevrev);
		for my $i (0..$#dirs) {
			my $d = $dirs[$i];
			my ($r, $u, $s) = get_times("$resultsdir/$prefixes{$d}$t.times");
			if ($i > 0 and defined $r and defined $prevr and $prevr > 0) {
				my $percent = 100.0 * ($r - $prevr) / $prevr;
				push @evolutions, { "percent"  => $percent,
						    "test"     => $t,
						    "prevrev"  => $prevrev,
						    "rev"      => $d,
						    "prevr"    => $prevr,
						    "r"        => $r,
						    "prevu"    => $prevu,
						    "u"        => $u,
						    "prevs"    => $prevs,
						    "s"        => $s};
			}
			($prevr, $prevu, $prevs, $prevrev) = ($r, $u, $s, $d);
		}
	}

	my @sorted_evolutions = sort { $b->{percent} <=> $a->{percent} } @evolutions;

	for my $e (@sorted_evolutions) {
		printf "%+.1f%%", $e->{percent};
		print " " . $e->{test};
		print " " . format_times($e->{prevr}, $e->{prevu}, $e->{prevs});
		print " " . format_times($e->{r}, $e->{u}, $e->{s});
		print " " . display_dir($e->{prevrev});
		print " " . display_dir($e->{rev});
		print "\n";
	}
}

sub print_codespeed_results {
	my ($subsection) = @_;

	my $project = "Git";

	my $executable = `uname -s -m`;
	chomp $executable;

	if ($subsection) {
		$executable .= ", " . $subsection;
	}

	my $environment;
	if ($reponame) {
		$environment = $reponame;
	} elsif (exists $ENV{GIT_PERF_REPO_NAME} and $ENV{GIT_PERF_REPO_NAME} ne "") {
		$environment = $ENV{GIT_PERF_REPO_NAME};
	} else {
		$environment = `uname -r`;
		chomp $environment;
	}

	my @data;

	for my $t (@subtests) {
		for my $d (@dirs) {
			my $commitid = $prefixes{$d};
			$commitid =~ s/^build_//;
			$commitid =~ s/\.$//;
			my ($result_value, $u, $s) = get_times("$resultsdir/$prefixes{$d}$t.times");

			my %vals = (
				"commitid" => $commitid,
				"project" => $project,
				"branch" => $dirnames{$d},
				"executable" => $executable,
				"benchmark" => $shorttests{$t} . " " . read_descr("$resultsdir/$t.descr"),
				"environment" => $environment,
				"result_value" => $result_value,
			    );
			push @data, \%vals;
		}
	}

	require JSON;
	print JSON::to_json(\@data, {utf8 => 1, pretty => 1, canonical => 1}), "\n";
}

binmode STDOUT, ":utf8" or die "PANIC on binmode: $!";

if ($codespeed) {
	print_codespeed_results($subsection);
} elsif (defined $sortby) {
	print_sorted_results($sortby);
} else {
	print_default_results();
}
