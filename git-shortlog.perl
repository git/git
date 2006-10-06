#!/usr/bin/perl -w

use strict;
use Getopt::Std;
use File::Basename qw(basename dirname);

our ($opt_h, $opt_n, $opt_s);
getopts('hns');

$opt_h && usage();

sub usage {
	print STDERR "Usage: ${\basename $0} [-h] [-n] [-s] < <log_data>\n";
        exit(1);
}

my (%mailmap);
my (%email);
my (%map);
my $pstate = 1;
my $n_records = 0;
my $n_output = 0;

sub shortlog_entry($$) {
	my ($name, $desc) = @_;
	my $key = $name;

	$desc =~ s#/pub/scm/linux/kernel/git/#/.../#g;
	$desc =~ s#\[PATCH\] ##g;

	# store description in array, in email->{desc list} map
	if (exists $map{$key}) {
		# grab ref
		my $obj = $map{$key};

		# add desc to array
		push(@$obj, $desc);
	} else {
		# create new array, containing 1 item
		my @arr = ($desc);

		# store ref to array
		$map{$key} = \@arr;
	}
}

# sort comparison function
sub by_name($$) {
	my ($a, $b) = @_;

	uc($a) cmp uc($b);
}
sub by_nbentries($$) {
	my ($a, $b) = @_;
	my $a_entries = $map{$a};
	my $b_entries = $map{$b};

	@$b_entries - @$a_entries || by_name $a, $b;
}

my $sort_method = $opt_n ? \&by_nbentries : \&by_name;

sub summary_output {
	my ($obj, $num, $key);

	foreach $key (sort $sort_method keys %map) {
		$obj = $map{$key};
		$num = @$obj;
		printf "%s: %u\n", $key, $num;
		$n_output += $num;
	}
}

sub shortlog_output {
	my ($obj, $num, $key, $desc);

	foreach $key (sort $sort_method keys %map) {
		$obj = $map{$key};
		$num = @$obj;

		# output author
		printf "%s (%u):\n", $key, $num;

		# output author's 1-line summaries
		foreach $desc (reverse @$obj) {
			print "  $desc\n";
			$n_output++;
		}

		# blank line separating author from next author
		print "\n";
	}
}

sub changelog_input {
	my ($author, $desc);

	while (<>) {
		# get author and email
		if ($pstate == 1) {
			my ($email);

			next unless /^[Aa]uthor:?\s*(.*?)\s*<(.*)>/;

			$n_records++;

			$author = $1;
			$email = $2;
			$desc = undef;

			# cset author fixups
			if (exists $mailmap{$email}) {
				$author = $mailmap{$email};
			} elsif (exists $mailmap{$author}) {
				$author = $mailmap{$author};
			} elsif (!$author) {
				$author = $email;
			}
			$email{$author}{$email}++;
			$pstate++;
		}

		# skip to blank line
		elsif ($pstate == 2) {
			next unless /^\s*$/;
			$pstate++;
		}

		# skip to non-blank line
		elsif ($pstate == 3) {
			next unless /^\s*?(.*)/;

			# skip lines that are obviously not
			# a 1-line cset description
			next if /^\s*From: /;

			chomp;
			$desc = $1;

			&shortlog_entry($author, $desc);

			$pstate = 1;
		}
	
		else {
			die "invalid parse state $pstate";
		}
	}
}

sub read_mailmap {
	my ($fh, $mailmap) = @_;
	while (<$fh>) {
		chomp;
		if (/^([^#].*?)\s*<(.*)>/) {
			$mailmap->{$2} = $1;
		}
	}
}

sub setup_mailmap {
	read_mailmap(\*DATA, \%mailmap);
	if (-f '.mailmap') {
		my $fh = undef;
		open $fh, '<', '.mailmap';
		read_mailmap($fh, \%mailmap);
		close $fh;
	}
}

sub finalize {
	#print "\n$n_records records parsed.\n";

	if ($n_records != $n_output) {
		die "parse error: input records != output records\n";
	}
	if (0) {
		for my $author (sort keys %email) {
			my $e = $email{$author};
			for my $email (sort keys %$e) {
				print STDERR "$author <$email>\n";
			}
		}
	}
}

&setup_mailmap;
&changelog_input;
$opt_s ? &summary_output : &shortlog_output;
&finalize;
exit(0);


__DATA__
#
# Even with git, we don't always have name translations.
# So have an email->real name table to translate the
# (hopefully few) missing names
#
Adrian Bunk <bunk@stusta.de>
Andreas Herrmann <aherrman@de.ibm.com>
Andrew Morton <akpm@osdl.org>
Andrew Vasquez <andrew.vasquez@qlogic.com>
Christoph Hellwig <hch@lst.de>
Corey Minyard <minyard@acm.org>
David Woodhouse <dwmw2@shinybook.infradead.org>
Domen Puncer <domen@coderock.org>
Douglas Gilbert <dougg@torque.net>
Ed L Cashin <ecashin@coraid.com>
Evgeniy Polyakov <johnpol@2ka.mipt.ru>
Felix Moeller <felix@derklecks.de>
Frank Zago <fzago@systemfabricworks.com>
Greg Kroah-Hartman <gregkh@suse.de>
James Bottomley <jejb@mulgrave.(none)>
James Bottomley <jejb@titanic.il.steeleye.com>
Jeff Garzik <jgarzik@pretzel.yyz.us>
Jens Axboe <axboe@suse.de>
Kay Sievers <kay.sievers@vrfy.org>
Mitesh shah <mshah@teja.com>
Morten Welinder <terra@gnome.org>
Morten Welinder <welinder@anemone.rentec.com>
Morten Welinder <welinder@darter.rentec.com>
Morten Welinder <welinder@troll.com>
Nguyen Anh Quynh <aquynh@gmail.com>
Paolo 'Blaisorblade' Giarrusso <blaisorblade@yahoo.it>
Peter A Jonsson <pj@ludd.ltu.se>
Ralf Wildenhues <Ralf.Wildenhues@gmx.de>
Rudolf Marek <R.Marek@sh.cvut.cz>
Rui Saraiva <rmps@joel.ist.utl.pt>
Sachin P Sant <ssant@in.ibm.com>
Santtu Hyrkk,Av(B <santtu.hyrkko@gmail.com>
Simon Kelley <simon@thekelleys.org.uk>
Tejun Heo <htejun@gmail.com>
Tony Luck <tony.luck@intel.com>
