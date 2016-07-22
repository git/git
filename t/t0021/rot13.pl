#!/usr/bin/env perl
#
# Example implementation for the Git filter protocol version 1
# See Documentation/gitattributes.txt, section "Filter Protocol"
#

use strict;
use warnings;
use autodie;

sub rot13 {
    my ($str) = @_;
    $str =~ y/A-Za-z/N-ZA-Mn-za-m/;
    return $str;
}

$| = 1; # autoflush STDOUT

open my $debug, ">>", "output.log";
$debug->autoflush(1);

print $debug "start\n";

print STDOUT "git-filter-protocol\nversion 1";
print $debug "wrote version\n";

while (1) {
    my $command = <STDIN>;
    unless (defined($command)) {
        exit();
    }
    chomp $command;
    print $debug "IN: $command";
    my $filename = <STDIN>;
    chomp $filename;
    print $debug " $filename";
    my $filelen  = <STDIN>;
    chomp $filelen;
    print $debug " $filelen";

    $filelen = int($filelen);
    my $output;

    if ( $filelen > 0 ) {
        my $input;
        {
            binmode(STDIN);
            my $bytes_read = 0;
            $bytes_read = read STDIN, $input, $filelen;
            if ( $bytes_read != $filelen ) {
                die "not enough to read";
            }
            print $debug " [OK] -- ";
        }

        if ( $command eq "clean") {
            $output = rot13($input);
        }
        elsif ( $command eq "smudge" ) {
            $output = rot13($input);
        }
        else {
            die "bad command\n";
        }
    }

    my $output_len = length($output);
    print STDOUT "$output_len\n";
    print $debug "OUT: $output_len";
    if ( $output_len > 0 ) {
        if ( ($command eq "clean" and $filename eq "clean-write-fail.r") or
             ($command eq "smudge" and $filename eq "smudge-write-fail.r") ) {
            print STDOUT "fail";
            print $debug " [FAIL]\n"
        } else {
            print STDOUT $output;
            print $debug " [OK]\n";
        }
    }
}
