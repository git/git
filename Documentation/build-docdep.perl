#!/usr/bin/perl

my %include = ();

for my $text (<git-*.txt>) {
    open I, '<', $text || die "cannot read: $text";
    (my $base = $text) =~ s/\.txt$//;
    while (<I>) {
	if (/^include::/) {
	    chomp;
	    s/^include::\s*//;
	    s/\[\]//;
	    $include{$base}{$_} = 1;
	}
    }
    close I;
}

# Do we care about chained includes???

while (my ($base, $included) = each %include) {
    my ($suffix) = '1';
    if ($base eq 'git') {
	$suffix = '7'; # yuck...
    }
    print "$base.html $base.$suffix : ", join(" ", keys %$included), "\n";
}

