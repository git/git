#!/usr/bin/perl

use warnings FATAL => 'all';
use strict;

# Highlight by reversing foreground and background. You could do
# other things like bold or underline if you prefer.
my $HIGHLIGHT   = "\x1b[7m";
my $UNHIGHLIGHT = "\x1b[27m";
my $COLOR = qr/\x1b\[[0-9;]*m/;
my $BORING = qr/$COLOR|\s/;

my @removed;
my @added;
my $in_hunk;

while (<>) {
	if (!$in_hunk) {
		print;
		$in_hunk = /^$COLOR*\@/;
	}
	elsif (/^$COLOR*-/) {
		push @removed, $_;
	}
	elsif (/^$COLOR*\+/) {
		push @added, $_;
	}
	else {
		show_hunk(\@removed, \@added);
		@removed = ();
		@added = ();

		print;
		$in_hunk = /^$COLOR*[\@ ]/;
	}

	# Most of the time there is enough output to keep things streaming,
	# but for something like "git log -Sfoo", you can get one early
	# commit and then many seconds of nothing. We want to show
	# that one commit as soon as possible.
	#
	# Since we can receive arbitrary input, there's no optimal
	# place to flush. Flushing on a blank line is a heuristic that
	# happens to match git-log output.
	if (!length) {
		local $| = 1;
	}
}

# Flush any queued hunk (this can happen when there is no trailing context in
# the final diff of the input).
show_hunk(\@removed, \@added);

exit 0;

sub show_hunk {
	my ($a, $b) = @_;

	# If one side is empty, then there is nothing to compare or highlight.
	if (!@$a || !@$b) {
		print @$a, @$b;
		return;
	}

	# If we have mismatched numbers of lines on each side, we could try to
	# be clever and match up similar lines. But for now we are simple and
	# stupid, and only handle multi-line hunks that remove and add the same
	# number of lines.
	if (@$a != @$b) {
		print @$a, @$b;
		return;
	}

	my @queue;
	for (my $i = 0; $i < @$a; $i++) {
		my ($rm, $add) = highlight_pair($a->[$i], $b->[$i]);
		print $rm;
		push @queue, $add;
	}
	print @queue;
}

sub highlight_pair {
	my @a = split_line(shift);
	my @b = split_line(shift);

	# Find common prefix, taking care to skip any ansi
	# color codes.
	my $seen_plusminus;
	my ($pa, $pb) = (0, 0);
	while ($pa < @a && $pb < @b) {
		if ($a[$pa] =~ /$COLOR/) {
			$pa++;
		}
		elsif ($b[$pb] =~ /$COLOR/) {
			$pb++;
		}
		elsif ($a[$pa] eq $b[$pb]) {
			$pa++;
			$pb++;
		}
		elsif (!$seen_plusminus && $a[$pa] eq '-' && $b[$pb] eq '+') {
			$seen_plusminus = 1;
			$pa++;
			$pb++;
		}
		else {
			last;
		}
	}

	# Find common suffix, ignoring colors.
	my ($sa, $sb) = ($#a, $#b);
	while ($sa >= $pa && $sb >= $pb) {
		if ($a[$sa] =~ /$COLOR/) {
			$sa--;
		}
		elsif ($b[$sb] =~ /$COLOR/) {
			$sb--;
		}
		elsif ($a[$sa] eq $b[$sb]) {
			$sa--;
			$sb--;
		}
		else {
			last;
		}
	}

	if (is_pair_interesting(\@a, $pa, $sa, \@b, $pb, $sb)) {
		return highlight_line(\@a, $pa, $sa),
		       highlight_line(\@b, $pb, $sb);
	}
	else {
		return join('', @a),
		       join('', @b);
	}
}

sub split_line {
	local $_ = shift;
	return map { /$COLOR/ ? $_ : (split //) }
	       split /($COLOR*)/;
}

sub highlight_line {
	my ($line, $prefix, $suffix) = @_;

	return join('',
		@{$line}[0..($prefix-1)],
		$HIGHLIGHT,
		@{$line}[$prefix..$suffix],
		$UNHIGHLIGHT,
		@{$line}[($suffix+1)..$#$line]
	);
}

# Pairs are interesting to highlight only if we are going to end up
# highlighting a subset (i.e., not the whole line). Otherwise, the highlighting
# is just useless noise. We can detect this by finding either a matching prefix
# or suffix (disregarding boring bits like whitespace and colorization).
sub is_pair_interesting {
	my ($a, $pa, $sa, $b, $pb, $sb) = @_;
	my $prefix_a = join('', @$a[0..($pa-1)]);
	my $prefix_b = join('', @$b[0..($pb-1)]);
	my $suffix_a = join('', @$a[($sa+1)..$#$a]);
	my $suffix_b = join('', @$b[($sb+1)..$#$b]);

	return $prefix_a !~ /^$COLOR*-$BORING*$/ ||
	       $prefix_b !~ /^$COLOR*\+$BORING*$/ ||
	       $suffix_a !~ /^$BORING*$/ ||
	       $suffix_b !~ /^$BORING*$/;
}
