#!/usr/bin/perl
use strict;
use warnings;
use JSON;

sub dump_array {
    my ($label_in, $ary_ref) = @_;
    my @ary = @$ary_ref;

    for ( my $i = 0; $i <= $#{ $ary_ref }; $i++ )
    {
	my $label = "$label_in\[$i\]";
	dump_item($label, $ary[$i]);
    }
}

sub dump_hash {
    my ($label_in, $obj_ref) = @_;
    my %obj = %$obj_ref;

    foreach my $k (sort keys %obj) {
	my $label = (length($label_in) > 0) ? "$label_in.$k" : "$k";
	my $value = $obj{$k};

	dump_item($label, $value);
    }
}

sub dump_item {
    my ($label_in, $value) = @_;
    if (ref($value) eq 'ARRAY') {
	print "$label_in array\n";
	dump_array($label_in, $value);
    } elsif (ref($value) eq 'HASH') {
	print "$label_in hash\n";
	dump_hash($label_in, $value);
    } elsif (defined $value) {
	print "$label_in $value\n";
    } else {
	print "$label_in null\n";
    }
}

my $row = 0;
while (<>) {
    my $data = decode_json( $_ );
    my $label = "row[$row]";

    dump_hash($label, $data);
    $row++;
}

