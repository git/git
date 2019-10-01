#!/bin/sh

# If "one-time-sed" exists in $HTTPD_ROOT_PATH, run sed on the HTTP response,
# using the contents of "one-time-sed" as the sed command to be run. If the
# response was modified as a result, delete "one-time-sed" so that subsequent
# HTTP responses are no longer modified.
#
# This can be used to simulate the effects of the repository changing in
# between HTTP request-response pairs.
if [ -e one-time-sed ]; then
	"$GIT_EXEC_PATH/git-http-backend" >out
	sed "$(cat one-time-sed)" <out >out_modified

	if diff out out_modified >/dev/null; then
		cat out
	else
		cat out_modified
		rm one-time-sed
	fi
else
	"$GIT_EXEC_PATH/git-http-backend"
fi
