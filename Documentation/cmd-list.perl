#!/usr/bin/perl -w

use File::Compare qw(compare);

sub format_one {
	my ($source_dir, $out, $nameattr) = @_;
	my ($name, $attr) = @$nameattr;
	my ($path) = "$source_dir/Documentation/$name.txt";
	my ($state, $description);
	my $mansection;
	$state = 0;
	open I, '<', "$path" or die "No such file $path.txt";
	while (<I>) {
		if (/^(?:git|scalar)[a-z0-9-]*\(([0-9])\)$/) {
			$mansection = $1;
			next;
		}
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
		die "No description found in $path.txt";
	}
	if (my ($verify_name, $text) = ($description =~ /^($name) - (.*)/)) {
		print $out "linkgit:$name\[$mansection\]::\n\t";
		if ($attr =~ / deprecated /) {
			print $out "(deprecated) ";
		}
		print $out "$text.\n\n";
	}
	else {
		die "Description does not match $name: $description";
	}
}

my ($source_dir, $build_dir, @categories) = @ARGV;

open IN, "<$source_dir/command-list.txt";
while (<IN>) {
	last if /^### command list/;
}

my %cmds = ();
for (sort <IN>) {
	next if /^#/;

	chomp;
	my ($name, $cat, $attr) = /^(\S+)\s+(.*?)(?:\s+(.*))?$/;
	$attr = '' unless defined $attr;
	push @{$cmds{$cat}}, [$name, " $attr "];
}
close IN;

for my $out (@categories) {
	my ($cat) = $out =~ /^cmds-(.*)\.txt$/;
	my ($path) = "$build_dir/$out";
	open O, '>', "$path+" or die "Cannot open output file $out+";
	for (@{$cmds{$cat}}) {
		format_one($source_dir, \*O, $_);
	}
	close O;

	if (-f "$path" && compare("$path", "$path+") == 0) {
		unlink "$path+";
	}
	else {
		rename "$path+", "$path";
	}
}
