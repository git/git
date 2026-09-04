#!/bin/sh

# If "one-time-script" exists in $HTTPD_ROOT_PATH, run the script on the HTTP
# response. If the response was modified as a result, delete "one-time-script"
# so that subsequent HTTP responses are no longer modified.
#
# This can be used to simulate the effects of the repository changing in
# between HTTP request-response pairs.
test -f one-time-script || exec "$GIT_EXEC_PATH/git-http-backend"

LC_ALL=C
export LC_ALL

out=out.$$
modified=out-modified.$$
"$GIT_EXEC_PATH/git-http-backend" >"$out"

# Since Apache can execute this script for multiple requests
# concurrently, we chain "rm one-time-script" with the logic
# for generating a modified response. If the "rm" ran separately,
# a concurrent request could pass the "test -f" above and
# erroneously result in multiple modified responses or an empty
# body depending on the race state.
#
# We discard stderr for ./one-time-script since it is possible
# ./one-time-script has been removed already, which is expected
# sometimes. In this case, the unmodified response will be returned.
if ./one-time-script "$out" 2>/dev/null >"$modified" &&
   ! cmp -s "$out" "$modified" &&
   rm one-time-script 2>/dev/null
then
	cat "$modified"
else
	cat "$out"
fi
rm -f "$out" "$modified"
