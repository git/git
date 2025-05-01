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

${0%/*}/check-unsafe-assertions.sh

save_good_tree
