#!/usr/bin/perl -w
#
# Copyright (c) 2005 Junio C Hamano
#
# Read .git/FETCH_HEAD and make a human readable merge message
# by grouping branches and tags together to form a single line.

use strict;

my @src;
my %src;
sub andjoin {
	my ($label, $labels, $stuff) = @_;
	my $l = scalar @$stuff;
	my $m = '';
	if ($l == 0) {
		return ();
	}
	if ($l == 1) {
		$m = "$label$stuff->[0]";
	}
	else {
		$m = ("$labels" .
		      join (', ', @{$stuff}[0..$l-2]) .
		      " and $stuff->[-1]");
	}
	return ($m);
}

while (<>) {
	my ($bname, $tname, $gname, $src);
	chomp;
	s/^[0-9a-f]*	//;
	next if (/^not-for-merge/);
	s/^	//;
	if (s/ of (.*)$//) {
		$src = $1;
	} else {
		# Pulling HEAD
		$src = $_;
		$_ = 'HEAD';
	}
	if (! exists $src{$src}) {
		push @src, $src;
		$src{$src} = {
			BRANCH => [],
			TAG => [],
			GENERIC => [],
			# &1 == has HEAD.
			# &2 == has others.
			HEAD_STATUS => 0,
		};
	}
	if (/^branch (.*)$/) {
		push @{$src{$src}{BRANCH}}, $1;
		$src{$src}{HEAD_STATUS} |= 2;
	}
	elsif (/^tag (.*)$/) {
		push @{$src{$src}{TAG}}, $1;
		$src{$src}{HEAD_STATUS} |= 2;
	}
	elsif (/^HEAD$/) {
		$src{$src}{HEAD_STATUS} |= 1;
	}
	else {
		push @{$src{$src}{GENERIC}}, $_;
		$src{$src}{HEAD_STATUS} |= 2;
	}
}

my @msg;
for my $src (@src) {
	if ($src{$src}{HEAD_STATUS} == 1) {
		# Only HEAD is fetched, nothing else.
		push @msg, $src;
		next;
	}
	my @this;
	if ($src{$src}{HEAD_STATUS} == 3) {
		# HEAD is fetched among others.
		push @this, andjoin('', '', ['HEAD']);
	}
	push @this, andjoin("branch ", "branches ",
			   $src{$src}{BRANCH});
	push @this, andjoin("tag ", "tags ",
			   $src{$src}{TAG});
	push @this, andjoin("commit ", "commits ",
			    $src{$src}{GENERIC});
	my $this = join(', ', @this);
	if ($src ne '.') {
		$this .= " of $src";
	}
	push @msg, $this;
}
print "Merge ", join("; ", @msg), "\n";
