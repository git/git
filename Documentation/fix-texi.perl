#!/usr/bin/perl -w

while (<>) {
	if (/^\@setfilename/) {
		$_ = "\@setfilename git.info\n";
	} elsif (/^\@direntry/) {
		print '@dircategory Development
@direntry
* Git: (git).           A fast distributed revision control system
@end direntry
';	}
	unless (/^\@direntry/../^\@end direntry/) {
		print;
	}
}
