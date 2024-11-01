#!/bin/sh
#
# Perform various static code analysis checks
#

. ${0%/*}/lib.sh

make coccicheck

set +x

fail=
for cocci_patch in contrib/coccinelle/*.patch
do
	if test -s "$cocci_patch"
	then
		echo "$(tput setaf 1)Coccinelle suggests the following changes in '$cocci_patch':$(tput sgr0)"
		cat "$cocci_patch"
		fail=UnfortunatelyYes
	fi
done

if test -n "$fail"
then
	echo "$(tput setaf 1)error: Coccinelle suggested some changes$(tput sgr0)"
	exit 1
fi

make hdr-check ||
exit 1

make check-pot

save_good_tree

# Run 'git clang-format' on changed *.c, *.cpp, *.h, and *.hpp files
if ! git clang-format-15 --diff --extensions=c,cpp,h,hpp HEAD; then
    echo "$(tput setaf 1)error: git clang-format made changes to the code$(tput sgr0)"
    exit 1
fi
