#!/usr/bin/env bash
#
# Script to trigger the a Git for Windows build and test run.
# Set the $GFW_CI_TOKEN as environment variable.
# Pass the branch (only branches on https://github.com/git/git are
# supported) and a commit hash.
#

test $# -ne 2 && echo "Unexpected number of parameters" && exit 1
test -z "$GFW_CI_TOKEN" && echo "GFW_CI_TOKEN not defined" && exit

BRANCH=$1
COMMIT=$2

gfwci () {
	local CURL_ERROR_CODE HTTP_CODE
	exec 3>&1
	HTTP_CODE=$(curl \
		-H "Authentication: Bearer $GFW_CI_TOKEN" \
		--silent --retry 5 --write-out '%{HTTP_CODE}' \
		--output >(sed "$(printf '1s/^\xef\xbb\xbf//')" >cat >&3) \
		"https://git-for-windows-ci.azurewebsites.net/api/TestNow?$1" \
	)
	CURL_ERROR_CODE=$?
	if test $CURL_ERROR_CODE -ne 0
	then
		return $CURL_ERROR_CODE
	fi
	if test "$HTTP_CODE" -ge 400 && test "$HTTP_CODE" -lt 600
	then
		return 127
	fi
}

# Trigger build job
BUILD_ID=$(gfwci "action=trigger&branch=$BRANCH&commit=$COMMIT&skipTests=false")
if test $? -ne 0
then
	echo "Unable to trigger Visual Studio Team Services Build"
	echo "$BUILD_ID"
	exit 1
fi

# Check if the $BUILD_ID contains a number
case $BUILD_ID in
''|*[!0-9]*) echo "Unexpected build number: $BUILD_ID" && exit 1
esac

echo "Visual Studio Team Services Build #${BUILD_ID}"

# Wait until build job finished
STATUS=
RESULT=
while true
do
	LAST_STATUS=$STATUS
	STATUS=$(gfwci "action=status&buildId=$BUILD_ID")
	test "$STATUS" = "$LAST_STATUS" || printf "\nStatus: $STATUS "
	printf "."

	case "$STATUS" in
	inProgress|postponed|notStarted) sleep 10               ;; # continue
		 "completed: succeeded") RESULT="success"; break;; # success
	*) echo "Unhandled status: $STATUS";               break;; # failure
	esac
done

# Print log
echo ""
echo ""
gfwci "action=log&buildId=$BUILD_ID" | cut -c 30-

# Set exit code for TravisCI
test "$RESULT" = "success"
