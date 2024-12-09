#!/bin/sh

# Description of the files in the repository:
#
#    imported-once.txt:
#
#       Imported once.  1.1 and 1.1.1.1 should be identical.
#
#    imported-twice.txt:
#
#       Imported twice.  HEAD should reflect the contents of the
#       second import (i.e., have the same contents as 1.1.1.2).
#
#    imported-modified.txt:
#
#       Imported, then modified on HEAD.  HEAD should reflect the
#       modification.
#
#    imported-modified-imported.txt:
#
#       Imported, then modified on HEAD, then imported again.
#
#    added-imported.txt,v:
#
#       Added with 'cvs add' to create 1.1, then imported with
#       completely different contents to create 1.1.1.1, therefore the
#       vendor branch was never the default branch.
#
#    imported-anonymously.txt:
#
#       Like imported-twice.txt, but with a vendor branch whose branch
#       tag has been removed.

test_description='git cvsimport handling of vendor branches'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-cvs.sh

setup_cvs_test_repository t9601

test_expect_success PERL 'import a module with a vendor branch' '

	git cvsimport -C module-git module

'

test_expect_success PERL 'check HEAD out of cvs repository' 'test_cvs_co main'

test_expect_success PERL 'check main out of git repository' 'test_git_co main'

test_expect_success PERL 'check a file that was imported once' '

	test_cmp_branch_file main imported-once.txt

'

test_expect_failure PERL 'check a file that was imported twice' '

	test_cmp_branch_file main imported-twice.txt

'

test_expect_success PERL 'check a file that was imported then modified on HEAD' '

	test_cmp_branch_file main imported-modified.txt

'

test_expect_success PERL 'check a file that was imported, modified, then imported again' '

	test_cmp_branch_file main imported-modified-imported.txt

'

test_expect_success PERL 'check a file that was added to HEAD then imported' '

	test_cmp_branch_file main added-imported.txt

'

test_expect_success PERL 'a vendor branch whose tag has been removed' '

	test_cmp_branch_file main imported-anonymously.txt

'

test_done
