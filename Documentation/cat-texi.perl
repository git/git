#!/usr/bin/perl -w

my @menu = ();
my $output = $ARGV[0];

open TMP, '>', "$output.tmp";

while (<STDIN>) {
	next if (/^\\input texinfo/../\@node Top/);
	next if (/^\@bye/ || /^\.ft/);
	if (s/^\@top (.*)/\@node $1,,,Top/) {
		push @menu, $1;
	}
	s/\(\@pxref{\[URLS\]}\)//;
	print TMP;
}
close TMP;

printf '\input texinfo
@setfilename gitman.info
@documentencoding us-ascii
@node Top,,%s
@top Git Manual Pages
@documentlanguage en
@menu
', $menu[0];

for (@menu) {
	print "* ${_}::\n";
}
print "\@end menu\n";
open TMP, '<', "$output.tmp";
while (<TMP>) {
	print;
}
close TMP;
print "\@bye\n";
unlink "$output.tmp";
