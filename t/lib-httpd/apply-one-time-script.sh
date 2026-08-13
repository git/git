#!/bin/sh

# If "one-time-script" exists in $HTTPD_ROOT_PATH, run the script on the HTTP
# response. If the response was modified as a result, delete "one-time-script"
# so that subsequent HTTP responses are no longer modified.
#
# This can be used to simulate the effects of the repository changing in
# between HTTP request-response pairs.
#
# Apache can run this CGI for several requests at the same time. For example, a
# partial fetch lazily fetches a missing object while the first response is
# still in flight. To stay correct, the helper removes the marker only after
# the response changes, and only with "rm" (without "-f"). The "rm" fails for
# every request except the one that removes the marker first. That request
# serves the modified body. Every other request serves its response unchanged.
# No request emits an empty body, which Apache would report as HTTP 500.
#
# A scratch file name includes the process ID ($$), so concurrent requests do
# not overwrite each other's files.
#
# The helper can run one-time-script more than once. It consumes the marker
# when the response changes (the "rm" after "cmp"), not when it runs the
# script. A request whose response is not the target runs the script, finds no
# change, and leaves the marker for a later request. This is safe because the
# scripts are stateless filters over the captured response.

test -f one-time-script || exec "$GIT_EXEC_PATH/git-http-backend"

LC_ALL=C
export LC_ALL

out=out.$$
modified=out-modified.$$
"$GIT_EXEC_PATH/git-http-backend" >"$out"

# one-time-script can be gone here: a concurrent request may have consumed it
# since the "test -f" above. Then "./one-time-script" fails, the exit status
# selects the unmodified body, and "2>/dev/null" discards the expected
# "no such file" message.
if ./one-time-script "$out" 2>/dev/null >"$modified" &&
   ! cmp -s "$out" "$modified" &&
   rm one-time-script 2>/dev/null
then
	cat "$modified"
else
	cat "$out"
fi
rm -f "$out" "$modified"
