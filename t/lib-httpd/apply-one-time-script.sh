#!/bin/sh

# If "one-time-script" exists in $HTTPD_ROOT_PATH, run the script on the HTTP
# response. If the response was modified as a result, delete "one-time-script"
# so that subsequent HTTP responses are no longer modified.
#
# This can be used to simulate the effects of the repository changing in
# between HTTP request-response pairs.
if test -f one-time-script
then
	LC_ALL=C
	export LC_ALL

	"$GIT_EXEC_PATH/git-http-backend" >out
	./one-time-script out >out_modified

	if cmp -s out out_modified
	then
		cat out
	else
		cat out_modified
		rm one-time-script
	fi
else
	"$GIT_EXEC_PATH/git-http-backend"
fi
