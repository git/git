#!/usr/bin/perl -w
my %mailmap = ();
open I, "<", ".mailmap";
while (<I>) {
	chomp;
	next if /^#/;
	if (my ($author, $mail) = /^(.*?)\s+<(.+)>$/) {
		$mailmap{$mail} = $author;
	}
}
close I;

my %mail2author = ();
open I, "git log --pretty='format:%ae	%an' |";
while (<I>) {
	chomp;
	my ($mail, $author) = split(/\t/, $_);
	next if exists $mailmap{$mail};
	$mail2author{$mail} ||= {};
	$mail2author{$mail}{$author} ||= 0;
	$mail2author{$mail}{$author}++;
}
close I;

while (my ($mail, $authorcount) = each %mail2author) {
	# %$authorcount is ($author => $count);
	# sort and show the names from the most frequent ones.
	my @names = (map { $_->[0] }
		sort { $b->[1] <=> $a->[1] }
		map { [$_, $authorcount->{$_}] }
		keys %$authorcount);
	if (1 < @names) {
		for (@names) {
			print "$_ <$mail>\n";
		}
	}
}

