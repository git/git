#!/usr/bin/perl

use strict;
use warnings;

my %SECTIONS;
{
	my $order = 0;
	%SECTIONS = (
		'NAME' => {
			required => 1,
			order => $order++,
		},
		'SYNOPSIS' => {
			required => 1,
			order => $order++,
		},
		'DESCRIPTION' => {
			required => 1,
			order => $order++,
		},
		'OPTIONS' => {
			order => $order++,
			required => 0,
		},
		'CONFIGURATION' => {
			order => $order++,
		},
		'BUGS' => {
			order => $order++,
		},
		'SEE ALSO' => {
			order => $order++,
		},
		'GIT' => {
			required => 1,
			order => $order++,
		},
	);
}
my $SECTION_RX = do {
	my ($names) = join "|", keys %SECTIONS;
	qr/^($names)$/s;
};

my $exit_code = 0;
sub report {
	my ($msg) = @_;
	print "$ARGV:$.: $msg\n";
	$exit_code = 1;
}

my $last_was_section;
my @actual_order;
while (my $line = <>) {
	chomp $line;
	if ($line =~ $SECTION_RX) {
		push @actual_order => $line;
		$last_was_section = 1;
		# Have no "last" section yet, processing NAME
		next if @actual_order == 1;

		my @expected_order = sort {
			$SECTIONS{$a}->{order} <=> $SECTIONS{$b}->{order}
		} @actual_order;

		my $expected_last = $expected_order[-2];
		my $actual_last = $actual_order[-2];
		if ($actual_last ne $expected_last) {
			report("section '$line' incorrectly ordered, comes after '$actual_last'");
		}
		next;
	}
	if ($last_was_section) {
		my $last_section = $actual_order[-1];
		if (length $last_section ne length $line) {
			report("dashes under '$last_section' should match its length!");
		}
		if ($line !~ /^-+$/) {
			report("dashes under '$last_section' should be '-' dashes!");
		}
		$last_was_section = 0;
	}

	if (eof) {
		# We have both a hash and an array to consider, for
		# convenience
		my %actual_sections;
		@actual_sections{@actual_order} = ();

		for my $section (sort keys %SECTIONS) {
			next if !$SECTIONS{$section}->{required} or exists $actual_sections{$section};
			report("has no required '$section' section!");
		}

		# Reset per-file state
		{
			@actual_order = ();
			# this resets our $. for each file
			close ARGV;
		}
	}
}

exit $exit_code;
