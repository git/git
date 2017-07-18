#!/usr/bin/perl -w

use File::Compare qw(compare);

sub format_one {
	my ($out, $nameattr) = @_;
	my ($name, $attr) = @$nameattr;
	my ($state, $description);
	$state = 0;
	open I, '<', "$name.txt" or die "No such file $name.txt";
	while (<I>) {
		if (/^NAME$/) {
			$state = 1;
			next;
		}
		if ($state == 1 && /^----$/) {
			$state = 2;
			next;
		}
		next if ($state != 2);
		chomp;
		$description = $_;
		last;
	}
	close I;
	if (!defined $description) {
		die "No description found in $name.txt";
	}
	if (my ($verify_name, $text) = ($description =~ /^($name) - (.*)/)) {
		print $out "linkgit:$name\[1\]::\n\t";
		if ($attr =~ / deprecated /) {
			print $out "(deprecated) ";
		}
		print $out "$text.\n\n";
	}
	else {
		die "Description does not match $name: $description";
	}
}

while (<>) {
	last if /^### command list/;
}

my %cmds = ();
for (sort <>) {
	next if /^#/;

	chomp;
	my ($name, $cat, $attr) = /^(\S+)\s+(.*?)(?:\s+(.*))?$/;
	$attr = '' unless defined $attr;
	push @{$cmds{$cat}}, [$name, " $attr "];
}

for my $cat (qw(ancillaryinterrogators
		ancillarymanipulators
		mainporcelain
		plumbinginterrogators
		plumbingmanipulators
		synchingrepositories
		foreignscminterface
		purehelpers
		synchelpers)) {
	my $out = "cmds-$cat.txt";
	open O, '>', "$out+" or die "Cannot open output file $out+";
	for (@{$cmds{$cat}}) {
		format_one(\*O, $_);
	}
	close O;

	if (-f "$out" && compare("$out", "$out+") == 0) {
		unlink "$out+";
	}
	else {
		print STDERR "$out\n";
		rename "$out+", "$out";
	}
}
