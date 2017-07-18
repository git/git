#!/bin/sh

test_description='git cvsimport timestamps'
. ./lib-cvs.sh

setup_cvs_test_repository t9604

test_expect_success PERL 'check timestamps are UTC (TZ=CST6CDT)' '

	TZ=CST6CDT git cvsimport -p"-x" -C module-1 module &&
	git cvsimport -p"-x" -C module-1 module &&
	(
		cd module-1 &&
		git log --format="%s %ai"
	) >actual-1 &&
	cat >expect-1 <<-EOF &&
	Rev 16 2006-10-29 07:00:01 +0000
	Rev 15 2006-10-29 06:59:59 +0000
	Rev 14 2006-04-02 08:00:01 +0000
	Rev 13 2006-04-02 07:59:59 +0000
	Rev 12 2005-12-01 00:00:00 +0000
	Rev 11 2005-11-01 00:00:00 +0000
	Rev 10 2005-10-01 00:00:00 +0000
	Rev  9 2005-09-01 00:00:00 +0000
	Rev  8 2005-08-01 00:00:00 +0000
	Rev  7 2005-07-01 00:00:00 +0000
	Rev  6 2005-06-01 00:00:00 +0000
	Rev  5 2005-05-01 00:00:00 +0000
	Rev  4 2005-04-01 00:00:00 +0000
	Rev  3 2005-03-01 00:00:00 +0000
	Rev  2 2005-02-01 00:00:00 +0000
	Rev  1 2005-01-01 00:00:00 +0000
	EOF
	test_cmp actual-1 expect-1
'

test_expect_success PERL 'check timestamps with author-specific timezones' '

	cat >cvs-authors <<-EOF &&
	user1=User One <user1@domain.org>
	user2=User Two <user2@domain.org> CST6CDT
	user3=User Three <user3@domain.org> EST5EDT
	user4=User Four <user4@domain.org> MST7MDT
	EOF
	git cvsimport -p"-x" -A cvs-authors -C module-2 module &&
	(
		cd module-2 &&
		git log --format="%s %ai %an"
	) >actual-2 &&
	cat >expect-2 <<-EOF &&
	Rev 16 2006-10-29 01:00:01 -0600 User Two
	Rev 15 2006-10-29 01:59:59 -0500 User Two
	Rev 14 2006-04-02 03:00:01 -0500 User Two
	Rev 13 2006-04-02 01:59:59 -0600 User Two
	Rev 12 2005-11-30 17:00:00 -0700 User Four
	Rev 11 2005-10-31 19:00:00 -0500 User Three
	Rev 10 2005-09-30 19:00:00 -0500 User Two
	Rev  9 2005-09-01 00:00:00 +0000 User One
	Rev  8 2005-07-31 18:00:00 -0600 User Four
	Rev  7 2005-06-30 20:00:00 -0400 User Three
	Rev  6 2005-05-31 19:00:00 -0500 User Two
	Rev  5 2005-05-01 00:00:00 +0000 User One
	Rev  4 2005-03-31 17:00:00 -0700 User Four
	Rev  3 2005-02-28 19:00:00 -0500 User Three
	Rev  2 2005-01-31 18:00:00 -0600 User Two
	Rev  1 2005-01-01 00:00:00 +0000 User One
	EOF
	test_cmp actual-2 expect-2
'

test_done
