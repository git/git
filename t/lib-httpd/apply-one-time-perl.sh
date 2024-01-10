#!/bin/sh

# If "one-time-perl" exists in $HTTPD_ROOT_PATH, run perl on the HTTP response,
# using the contents of "one-time-perl" as the perl command to be run. If the
# response was modified as a result, delete "one-time-perl" so that subsequent
# HTTP responses are no longer modified.
#
# This can be used to simulate the effects of the repository changing in
# between HTTP request-response pairs.
if test -f one-time-perl
then
	LC_ALL=C
	export LC_ALL

	"$GIT_EXEC_PATH/git-http-backend" >out
	"$PERL_PATH" -pe "$(cat one-time-perl)" out >out_modified

	if cmp -s out out_modified
	then
		cat out
	else
		cat out_modified
		rm one-time-perl
	fi
else
	"$GIT_EXEC_PATH/git-http-backend"
fi
