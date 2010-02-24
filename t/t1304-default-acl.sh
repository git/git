#!/bin/sh
#
# Copyright (c) 2010 Matthieu Moy
#

test_description='Test repository with default ACL'

# Create the test repo with restrictive umask
# => this must come before . ./test-lib.sh
umask 077

. ./test-lib.sh

# We need an arbitrary other user give permission to using ACLs. root
# is a good candidate: exists on all unices, and it has permission
# anyway, so we don't create a security hole running the testsuite.

if ! setfacl -m u:root:rwx .; then
    say "Skipping ACL tests: unable to use setfacl"
    test_done
fi

modebits () {
	ls -l "$1" | sed -e 's|^\(..........\).*|\1|'
}

check_perms_and_acl () {
	actual=$(modebits "$1") &&
	case "$actual" in
	-r--r-----*)
		: happy
		;;
	*)
		echo "Got permission '$actual', expected '-r--r-----'"
		false
		;;
	esac &&
	getfacl "$1" > actual &&
	grep -q "user:root:rwx" actual &&
	grep -q "user:${LOGNAME}:rwx" actual &&
	grep -q "mask::r--" actual &&
	grep -q "group::---" actual || false
}

dirs_to_set="./ .git/ .git/objects/ .git/objects/pack/"

test_expect_success 'Setup test repo' '
	setfacl -m u:root:rwx          $dirs_to_set &&
	setfacl -d -m u:"$LOGNAME":rwx $dirs_to_set &&
	setfacl -d -m u:root:rwx       $dirs_to_set &&

	touch file.txt &&
	git add file.txt &&
	git commit -m "init"
'

test_expect_success 'Objects creation does not break ACLs with restrictive umask' '
	# SHA1 for empty blob
	check_perms_and_acl .git/objects/e6/9de29bb2d1d6434b8b29ae775ad8c2e48c5391
'

test_expect_success 'git gc does not break ACLs with restrictive umask' '
	git gc &&
	check_perms_and_acl .git/objects/pack/*.pack
'

test_done
