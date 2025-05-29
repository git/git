#!/bin/sh

awk '
	/^@setfilename/{
		print "@setfilename git.info"
		next
	}
	/^@direntry/{
		direntry=1
		print "@dircategory Development"
		print "@direntry"
		print "* Git: (git).           A fast distributed revision control system"
		print "@end direntry"
		next
	}
	/^@end direntry/{
		direntry=0
		next
	}
	!direntry
'
