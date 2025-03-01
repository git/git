#!/usr/bin/perl

my ($build_dir) = @ARGV;
my %include = ();
my %included = ();

for my $adoc (<*.adoc>) {
    open I, '<', $adoc || die "cannot read: $adoc";
    while (<I>) {
	if (/^include::/) {
	    chomp;
	    s/^include::\s*//;
	    s/\[\]//;
	    s/{build_dir}/${build_dir}/;
	    $include{$adoc}{$_} = 1;
	    $included{$_} = 1;
	}
    }
    close I;
}

# Do we care about chained includes???
my $changed = 1;
while ($changed) {
    $changed = 0;
    while (my ($adoc, $included) = each %include) {
	for my $i (keys %$included) {
	    # $adoc has include::$i; if $i includes $j
	    # $adoc indirectly includes $j.
	    if (exists $include{$i}) {
		for my $j (keys %{$include{$i}}) {
		    if (!exists $include{$adoc}{$j}) {
			$include{$adoc}{$j} = 1;
			$included{$j} = 1;
			$changed = 1;
		    }
		}
	    }
	}
    }
}

foreach my $adoc (sort keys %include) {
    my $included = $include{$adoc};
    if (! exists $included{$adoc} &&
	(my $base = $adoc) =~ s/\.adoc$//) {
	print "$base.html $base.xml : ", join(" ", sort keys %$included), "\n";
    }
}
