#!/bin/sh
#
# Print output of failing tests
#

. ${0%/*}/lib-travisci.sh

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
