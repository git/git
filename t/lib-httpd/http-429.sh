#!/bin/sh

# Script to return HTTP 429 Too Many Requests responses for testing retry logic.
# Usage: /http_429/<test-context>/<retry-after-value>/<repo-path>
#
# The test-context is a unique identifier for each test to isolate state files.
# The retry-after-value can be:
#   - A number (e.g., "1", "2", "100") - sets Retry-After header to that many seconds
#   - "none" - no Retry-After header
#   - "invalid" - invalid Retry-After format
#   - "permanent" - always return 429 (never succeed)
#   - An HTTP-date string (RFC 2822 format) - sets Retry-After to that date
#
# On first call, returns 429. On subsequent calls (after retry), forwards to git-http-backend
# unless retry-after-value is "permanent".

# Extract test context, retry-after value and repo path from PATH_INFO
# PATH_INFO format: /<test-context>/<retry-after-value>/<repo-path>
path_info="${PATH_INFO#/}"  # Remove leading slash
test_context="${path_info%%/*}"  # Get first component (test context)
remaining="${path_info#*/}"  # Get rest
retry_after="${remaining%%/*}"  # Get second component (retry-after value)
repo_path="${remaining#*/}"  # Get rest (repo path)

# Extract repository name from repo_path (e.g., "repo.git" from "repo.git/info/refs")
# The repo name is the first component before any "/"
repo_name="${repo_path%%/*}"

# Use current directory (HTTPD_ROOT_PATH) for state file
# Create a safe filename from test_context, retry_after and repo_name
# This ensures all requests for the same test context share the same state file
safe_name=$(echo "${test_context}-${retry_after}-${repo_name}" | tr '/' '_' | tr -cd 'a-zA-Z0-9_-')
state_file="http-429-state-${safe_name}"

# Check if this is the first call (no state file exists)
if test -f "$state_file"
then
	# Already returned 429 once, forward to git-http-backend
	# Set PATH_INFO to just the repo path (without retry-after value)
	# Set GIT_PROJECT_ROOT so git-http-backend can find the repository
	# Use exec to replace this process so git-http-backend gets the updated environment
	PATH_INFO="/$repo_path"
	export PATH_INFO
	# GIT_PROJECT_ROOT points to the document root where repositories are stored
	# The script runs from HTTPD_ROOT_PATH, and www/ is the document root
	if test -z "$GIT_PROJECT_ROOT"
	then
		# Construct path: current directory (HTTPD_ROOT_PATH) + /www
		GIT_PROJECT_ROOT="$(pwd)/www"
		export GIT_PROJECT_ROOT
	fi
	exec "$GIT_EXEC_PATH/git-http-backend"
fi

# Mark that we've returned 429
touch "$state_file"

# Output HTTP 429 response
printf "Status: 429 Too Many Requests\r\n"

# Set Retry-After header based on retry_after value
case "$retry_after" in
	none)
		# No Retry-After header
		;;
	invalid)
		printf "Retry-After: invalid-format-123abc\r\n"
		;;
	permanent)
		# Always return 429, don't set state file for success
		rm -f "$state_file"
		printf "Retry-After: 1\r\n"
		printf "Content-Type: text/plain\r\n"
		printf "\r\n"
		printf "Permanently rate limited\n"
		exit 0
		;;
	*)
		# Check if it's a number
		case "$retry_after" in
			[0-9]*)
				# Numeric value
				printf "Retry-After: %s\r\n" "$retry_after"
				;;
			*)
				# Assume it's an HTTP-date format (passed as-is, URL decoded)
				# Apache may URL-encode the path, so decode common URL-encoded characters
				# %20 = space, %2C = comma, %3A = colon
				retry_value=$(echo "$retry_after" | sed -e 's/%20/ /g' -e 's/%2C/,/g' -e 's/%3A/:/g')
				printf "Retry-After: %s\r\n" "$retry_value"
				;;
		esac
		;;
esac

printf "Content-Type: text/plain\r\n"
printf "\r\n"
printf "Rate limited\n"
