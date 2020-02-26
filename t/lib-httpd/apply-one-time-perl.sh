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
	perl -pe "$(cat one-time-perl)" out >out_modified

echo "before applying one-time $(cat one-time-perl)" >&2
hexdump -C out >&2
echo "after applying one-time $(cat one-time-perl)" >&2
hexdump -C out_modified >&2

	if cmp -s out out_modified
	then
		cat out
	else
		cat out_modified
#		rm one-time-perl
mv one-time-perl one-time-perl.$(($(ls one-time-perl.* 2>/dev/null | wc -l | tr -dc 0-9)+1))
	fi
else
	"$GIT_EXEC_PATH/git-http-backend"
fi
