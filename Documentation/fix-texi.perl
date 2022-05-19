#!/usr/bin/perl -w

while (<>) {
	if (/^\@setfilename/) {
		$_ = "\@setfilename but.info\n";
	} elsif (/^\@direntry/) {
		print '@dircategory Development
@direntry
* Git: (but).           A fast distributed revision control system
@end direntry
';	}
	unless (/^\@direntry/../^\@end direntry/) {
		print;
	}
}
