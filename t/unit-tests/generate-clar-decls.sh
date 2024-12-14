#!/bin/sh

if test $# -lt 2
then
	echo "USAGE: $0 <OUTPUT> <SUITE>..." 2>&1
	exit 1
fi

OUTPUT="$1"
shift

for suite in "$@"
do
	suite_name=$(basename "$suite")
	suite_name=${suite_name%.c}
	suite_name=${suite_name#u-}
	sed -ne "s/^\(void test_${suite_name}__[a-zA-Z_0-9][a-zA-Z_0-9]*(void)\)$/extern \1;/p" "$suite" ||
	exit 1
done >"$OUTPUT"
