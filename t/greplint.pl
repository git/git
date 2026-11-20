#!/usr/bin/env perl

# Detect bare 'grep' used as a test assertion
use warnings;
use strict;
use File::Basename;
do(dirname($0) . "/lib-shell-parser.pl")
	or die "$0: failed to load lib-shell-parser.pl: $@$!\n";
do(dirname($0) . "/lib-greplint.pl")
	or die "$0: failed to load lib-greplint.pl: $@$!\n";

my $exit_code = 0;

package main;

for my $file (@ARGV) {
	open(my $fh, '<:unix:crlf', $file) or die "$0: $file: $!\n";
	my @raw_lines = <$fh>;
	close $fh;
	my $s = join('', @raw_lines);
	my $parser = GrepLintParser->new(\$s);
	$parser->{file} = $file;
	$parser->{raw_lines} = \@raw_lines;
	$parser->parse();
	print "$_\n" for @{$parser->{output}};
	$exit_code ||= $parser->{nerrs} > 0;
}
exit $exit_code;
