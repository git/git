#!/usr/bin/perl

my ($fsck_h, $fsck_msgids_txt, $okfile) = @ARGV;

my (%in_fsck_h, $fh, $bad);

open($fh, "<", "$fsck_h") or die;
while (<$fh>) {
	if (/^\s+FUNC\(([0-9A-Z_]+), ([A-Z]+)\)/) {
		my ($name, $severity) = ($1, $2);
		my ($first) = 1;
		$name = join('',
			     map {
				     y/A-Z/a-z/;
				     if (!$first) {
					     s/^(.)/uc($1)/e;
				     } else {
					     $first = 0;
				     }
				     $_;
			     }
			     split(/_/, $name));
		$in_fsck_h{$name} = $severity;
	}
}
close($fh);

open($fh, "<", "$fsck_msgids_txt") or die;
my ($previous, $current);
while (<$fh>) {
	if (!defined $current) {
		if (/^\`([a-zA-Z0-9]*)\`::/) {
			$current = $1;
			if ((defined $previous) &&
			    ($current le $previous)) {
				print STDERR "$previous >= $current in doc\n";
				$bad = 1;
			}
		}
	} elsif (/^\s+\(([A-Z]+)\) /) {
		my ($level) = $1;
		if (!exists $in_fsck_h{$current}) {
			print STDERR "$current does not exist in fsck.h\n";
			$bad = 1;
		} elsif ($in_fsck_h{$current} eq "") {
			print STDERR "$current defined twice\n";
			$bad = 1;
		} elsif ($in_fsck_h{$current} ne $level) {
			print STDERR "$current severity $level != $in_fsck_h{$current}\n";
			$bad = 1;
		}
		$previous = $current;
		$in_fsck_h{$current} = ""; # mark as seen.
		undef $current;
	}
}
close($fh);

for my $key (keys %in_fsck_h) {
	if ($in_fsck_h{$key} ne "") {
		print STDERR "$key not explained in doc.\n";
		$bad = 1;
	}
}

die if ($bad);

open($fh, ">", "$okfile");
print $fh "good\n";
close($fh);
