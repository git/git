#!/usr/bin/perl

use warnings 'all';
use strict;
use Getopt::Long;

my $match_emails;
my $match_names;
my $order_by = 'count';
Getopt::Long::Configure(qw(bundling));
GetOptions(
	'emails|e!' => \$match_emails,
	'names|n!'  => \$match_names,
	'count|c'   => sub { $order_by = 'count' },
	'time|t'    => sub { $order_by = 'stamp' },
) or exit 1;
$match_emails = 1 unless $match_names;

my $email = {};
my $name = {};

open(my $fh, '-|', "git log --format='%at <%aE> %aN'");
while(<$fh>) {
	my ($t, $e, $n) = /(\S+) <(\S+)> (.*)/;
	mark($email, $e, $n, $t);
	mark($name, $n, $e, $t);
}
close($fh);

if ($match_emails) {
	foreach my $e (dups($email)) {
		foreach my $n (vals($email->{$e})) {
			show($n, $e, $email->{$e}->{$n});
		}
		print "\n";
	}
}
if ($match_names) {
	foreach my $n (dups($name)) {
		foreach my $e (vals($name->{$n})) {
			show($n, $e, $name->{$n}->{$e});
		}
		print "\n";
	}
}
exit 0;

sub mark {
	my ($h, $k, $v, $t) = @_;
	my $e = $h->{$k}->{$v} ||= { count => 0, stamp => 0 };
	$e->{count}++;
	$e->{stamp} = $t unless $t < $e->{stamp};
}

sub dups {
	my $h = shift;
	return grep { keys($h->{$_}) > 1 } keys($h);
}

sub vals {
	my $h = shift;
	return sort {
		$h->{$b}->{$order_by} <=> $h->{$a}->{$order_by}
	} keys($h);
}

sub show {
	my ($n, $e, $h) = @_;
	print "$n <$e> ($h->{$order_by})\n";
}
