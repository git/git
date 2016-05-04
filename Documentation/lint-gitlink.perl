#!/usr/bin/perl

use File::Find;
use Getopt::Long;

my $basedir = ".";
GetOptions("basedir=s" => \$basedir)
	or die("Cannot parse command line arguments\n");

my $found_errors = 0;

sub report {
	my ($where, $what, $error) = @_;
	print "$where: $error: $what\n";
	$found_errors = 1;
}

sub grab_section {
	my ($page) = @_;
	open my $fh, "<", "$basedir/$page.txt";
	my $firstline = <$fh>;
	chomp $firstline;
	close $fh;
	my ($section) = ($firstline =~ /.*\((\d)\)$/);
	return $section;
}

sub lint {
	my ($file) = @_;
	open my $fh, "<", $file
		or return;
	while (<$fh>) {
		my $where = "$file:$.";
		while (s/linkgit:((.*?)\[(\d)\])//) {
			my ($target, $page, $section) = ($1, $2, $3);

			# De-AsciiDoc
			$page =~ s/{litdd}/--/g;

			if ($page !~ /^git/) {
				report($where, $target, "nongit link");
				next;
			}
			if (! -f "$basedir/$page.txt") {
				report($where, $target, "no such source");
				next;
			}
			$real_section = grab_section($page);
			if ($real_section != $section) {
				report($where, $target,
					"wrong section (should be $real_section)");
				next;
			}
		}
	}
	close $fh;
}

sub lint_it {
	lint($File::Find::name) if -f && /\.txt$/;
}

if (!@ARGV) {
	find({ wanted => \&lint_it, no_chdir => 1 }, $basedir);
} else {
	for (@ARGV) {
		lint($_);
	}
}

exit $found_errors;
