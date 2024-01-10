#!/bin/sh

VALID_CREDS_FILE=custom-auth.valid
CHALLENGE_FILE=custom-auth.challenge

#
# If $VALID_CREDS_FILE exists in $HTTPD_ROOT_PATH, consider each line as a valid
# credential for the current request. Each line in the file is considered a
# valid HTTP Authorization header value. For example:
#
# Basic YWxpY2U6c2VjcmV0LXBhc3N3ZA==
#
# If $CHALLENGE_FILE exists in $HTTPD_ROOT_PATH, output the contents as headers
# in a 401 response if no valid authentication credentials were included in the
# request. For example:
#
# WWW-Authenticate: Bearer authorize_uri="id.example.com" p=1 q=0
# WWW-Authenticate: Basic realm="example.com"
#

if test -n "$HTTP_AUTHORIZATION" && \
	grep -Fqsx "${HTTP_AUTHORIZATION}" "$VALID_CREDS_FILE"
then
	# Note that although git-http-backend returns a status line, it
	# does so using a CGI 'Status' header. Because this script is an
	# No Parsed Headers (NPH) script, we must return a real HTTP
	# status line.
	# This is only a test script, so we don't bother to check for
	# the actual status from git-http-backend and always return 200.
	echo 'HTTP/1.1 200 OK'
	exec "$GIT_EXEC_PATH"/git-http-backend
fi

echo 'HTTP/1.1 401 Authorization Required'
if test -f "$CHALLENGE_FILE"
then
	cat "$CHALLENGE_FILE"
fi
echo
