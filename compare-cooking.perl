#!/usr/bin/perl -w

$SIG{'PIPE'} = 'IGNORE';

my ($old, $new);

if (@ARGV == 7) {
	# called as GIT_EXTERNAL_DIFF script
	$old = parse_cooking($ARGV[1]);
	$new = parse_cooking($ARGV[4]);
} else {
	# called with old and new
	$old = parse_cooking($ARGV[0]);
	$new = parse_cooking($ARGV[1]);
}
compare_cooking($old, $new);

################################################################

use File::Temp qw(tempfile);

sub compare_them {
	local($_);
	my ($a, $b, $force, $soft) = @_;

	if ($soft) {
		$plus = $minus = ' ';
	} else {
		$plus = '+';
		$minus = '-';
	}

	if (!defined $a->[0]) {
		return map { "$plus$_\n" } map { split(/\n/) } @{$b};
	} elsif (!defined $b->[0]) {
		return map { "$minus$_\n" } map { split(/\n/) } @{$a};
	} elsif (join('', @$a) eq join('', @$b)) {
		if ($force) {
			return map { " $_\n" } map { split(/\n/) } @{$a};
		} else {
			return ();
		}
	}
	my ($ah, $aname) = tempfile();
	my ($bh, $bname) = tempfile();
	my $cnt = 0;
	my @result = ();
	for (@$a) {
		print $ah $_;
		$cnt += tr/\n/\n/;
	}
	for (@$b) {
		print $bh $_;
		$cnt += tr/\n/\n/;
	}
	close $ah;
	close $bh;
	open(my $fh, "-|", 'diff', "-U$cnt", $aname, $bname);
	$cnt = 0;
	while (<$fh>) {
		next if ($cnt++ < 3);
		push @result, $_;
	}
	close $fh;
	unlink ($aname, $bname);
	return @result;
}

sub flush_topic {
	my ($cooking, $name, $desc) = @_;
	my $section = $cooking->{SECTIONS}[-1];

	return if (!defined $name);

	$desc =~ s/\s+\Z/\n/s;
	$desc =~ s/\A\s+//s;
	my $topic = +{
		IN_SECTION => $section,
		NAME => $name,
		DESC => $desc,
	};
	$cooking->{TOPICS}{$name} = $topic;
	push @{$cooking->{TOPIC_ORDER}}, $name;
}

sub parse_section {
	my ($cooking, @line) = @_;

	while (@line && $line[-1] =~ /^\s*$/) {
		pop @line;
	}
	return if (!@line);

	if (!exists $cooking->{SECTIONS}) {
		$cooking->{SECTIONS} = [];
		$cooking->{TOPICS} = {};
		$cooking->{TOPIC_ORDER} = [];
	}
	if (!exists $cooking->{HEADER}) {
		my $line = join('', @line);
		$line =~ s/\A.*?\n\n//s;
		$cooking->{HEADER} = $line;
		return;
	}
	if (!exists $cooking->{GREETING}) {
		$cooking->{GREETING} = join('', @line);
		return;
	}

	my ($section_name, $topic_name, $topic_desc);
	for (@line) {
		if (!defined $section_name && /^\[(.*)\]$/) {
			$section_name = $1;
			push @{$cooking->{SECTIONS}}, $section_name;
			next;
		}
		if (/^\* (\S+) /) {
			my $next_name = $1;
			flush_topic($cooking, $topic_name, $topic_desc);
			$topic_name = $next_name;
			$topic_desc = '';
		}
		$topic_desc .= $_;
	}
	flush_topic($cooking, $topic_name, $topic_desc);
}

sub dump_cooking {
	my ($cooking) = @_;
	print $cooking->{HEADER};
	print "-" x 50, "\n";
	print $cooking->{GREETING};
	for my $section_name (@{$cooking->{SECTIONS}}) {
		print "\n", "-" x 50, "\n";
		print "[$section_name]\n";
		for my $topic_name (@{$cooking->{TOPIC_ORDER}}) {
			$topic = $cooking->{TOPICS}{$topic_name};
			next if ($topic->{IN_SECTION} ne $section_name);
			print "\n", $topic->{DESC};
		}
	}
}

sub parse_cooking {
	my ($filename) = @_;
	my (%cooking, @current, $fh);
	open $fh, "<", $filename
	    or die "cannot open $filename: $!";
	while (<$fh>) {
		if (/^-{30,}$/) {
			parse_section(\%cooking, @current);
			@current = ();
			next;
		}
		push @current, $_;
	}
	close $fh;
	parse_section(\%cooking, @current);

	return \%cooking;
}

sub compare_topics {
	my ($a, $b) = @_;
	if (!@$a || !@$b) {
		print compare_them($a, $b, 1, 1);
		return;
	}

	# otherwise they both have title.
	$a = [map { "$_\n" } split(/\n/, join('', @$a))];
	$b = [map { "$_\n" } split(/\n/, join('', @$b))];
	my $atitle = shift @$a;
	my $btitle = shift @$b;
	print compare_them([$atitle], [$btitle], 1);

	my (@atail, @btail);
	while (@$a && $a->[-1] !~ /^\s/) {
		unshift @atail, pop @$a;
	}
	while (@$b && $b->[-1] !~ /^\s/) {
		unshift @btail, pop @$b;
	}
	print compare_them($a, $b);
	print compare_them(\@atail, \@btail);
}

sub compare_class {
	my ($fromto, $names, $topics) = @_;

	my (@where, %where);
	for my $name (@$names) {
		my $t = $topics->{$name};
		my ($a, $b, $in, $force);
		if ($t->{OLD} && $t->{NEW}) {
			$a = [$t->{OLD}{DESC}];
			$b = [$t->{NEW}{DESC}];
			if ($t->{OLD}{IN_SECTION} ne $t->{NEW}{IN_SECTION}) {
				$force = 1;
				$in = '';
			} else {
				$in = "[$t->{NEW}{IN_SECTION}]";
			}
		} elsif ($t->{OLD}) {
			$a = [$t->{OLD}{DESC}];
			$b = [];
			$in = "Was in [$t->{OLD}{IN_SECTION}]";
		} else {
			$a = [];
			$b = [$t->{NEW}{DESC}];
			$in = "[$t->{NEW}{IN_SECTION}]";
		}
		next if (defined $a->[0] &&
			 defined $b->[0] &&
			 $a->[0] eq $b->[0] && !$force);

		if (!exists $where{$in}) {
			push @where, $in;
			$where{$in} = [];
		}
		push @{$where{$in}}, [$a, $b];
	}

	return if (!@where);
	for my $in (@where) {
		my @bag = @{$where{$in}};
		if (defined $fromto && $fromto ne '') {
			print "\n", '-' x 50, "\n$fromto\n";
			$fromto = undef;
		}
		print "\n$in\n" if ($in ne '');
		for (@bag) {
			my ($a, $b) = @{$_};
			print "\n";
			compare_topics($a, $b);
		}
	}
}

sub compare_cooking {
	my ($old, $new) = @_;

	print compare_them([$old->{HEADER}], [$new->{HEADER}]);
	print compare_them([$old->{GREETING}], [$new->{GREETING}]);

	my (@sections, %sections, @topics, %topics, @fromto, %fromto);

	for my $section_name (@{$old->{SECTIONS}}, @{$new->{SECTIONS}}) {
		next if (exists $sections{$section_name});
		$sections{$section_name} = scalar @sections;
		push @sections, $section_name;
	}

	my $gone_class = "Gone topics";
	my $born_class = "Born topics";
	my $stay_class = "Other topics";

	push @fromto, $born_class;
	for my $topic_name (@{$old->{TOPIC_ORDER}}, @{$new->{TOPIC_ORDER}}) {
		next if (exists $topics{$topic_name});
		push @topics, $topic_name;

		my $oldtopic = $old->{TOPICS}{$topic_name};
		my $newtopic = $new->{TOPICS}{$topic_name};
		$topics{$topic_name} = +{
			OLD => $oldtopic,
			NEW => $newtopic,
		};
		my $oldsec = $oldtopic->{IN_SECTION};
		my $newsec = $newtopic->{IN_SECTION};
		if (defined $oldsec && defined $newsec) {
			if ($oldsec ne $newsec) {
				my $fromto =
				    "Moved from [$oldsec] to [$newsec]";
				if (!exists $fromto{$fromto}) {
					$fromto{$fromto} = [];
					push @fromto, $fromto;
				}
				push @{$fromto{$fromto}}, $topic_name;
			} else {
				push @{$fromto{$stay_class}}, $topic_name;
			}
		} elsif (defined $oldsec) {
			push @{$fromto{$gone_class}}, $topic_name;
		} else {
			push @{$fromto{$born_class}}, $topic_name;
		}
	}
	push @fromto, $stay_class;
	push @fromto, $gone_class;

	for my $fromto (@fromto) {
		compare_class($fromto, $fromto{$fromto}, \%topics);
	}
}
