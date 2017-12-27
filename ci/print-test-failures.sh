#!/bin/sh
#
# Print output of failing tests
#

. ${0%/*}/lib-travisci.sh

# Tracing executed commands would produce too much noise in the loop below.
set +x

if ! ls t/test-results/*.exit >/dev/null 2>/dev/null
then
	echo "Build job failed before the tests could have been run"
	exit
fi

for TEST_EXIT in t/test-results/*.exit
do
	if [ "$(cat "$TEST_EXIT")" != "0" ]
	then
		TEST_OUT="${TEST_EXIT%exit}out"
		echo "------------------------------------------------------------------------"
		echo "$(tput setaf 1)${TEST_OUT}...$(tput sgr0)"
		echo "------------------------------------------------------------------------"
		cat "${TEST_OUT}"
	fi
done
