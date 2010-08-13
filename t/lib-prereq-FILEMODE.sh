#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

if test "$(git config --bool core.filemode)" = false
then
	say 'filemode disabled on the filesystem'
else
	test_set_prereq FILEMODE
fi
