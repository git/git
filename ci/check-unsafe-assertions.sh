#!/bin/sh

make CHECK_ASSERTION_SIDE_EFFECTS=1 >compiler_output 2>compiler_error
if test $? != 0
then
	echo >&2 "ERROR: The compiler could not verify the following assert()"
	echo >&2 "       calls are free of side-effects.  Please replace with"
	echo >&2 "       ASSERT() calls."
	grep undefined.reference.to..not_supposed_to_survive compiler_error |
		sed -e s/:[^:]*$// | sort | uniq | tr ':' ' ' |
		while read f l
		do
			printf "${f}:${l}\n  "
			awk -v start="$l" 'NR >= start { print; if (/\);/) exit }' $f
		done
	exit 1
fi
rm compiler_output compiler_error
