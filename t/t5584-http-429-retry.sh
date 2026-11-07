#!/bin/sh

test_description='test HTTP 429 Too Many Requests retry logic'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-httpd.sh

start_httpd

test_expect_success 'setup test repository' '
	test_commit initial &&
	git clone --bare . "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/repo.git" config http.receivepack true
'

# This test suite uses a special HTTP 429 endpoint at /http_429/ that simulates
# rate limiting. The endpoint format is:
#   /http_429/<test-context>/<retry-after-value>/<repo-path>
# The http-429.sh script (in t/lib-httpd) returns a 429 response with the
# specified Retry-After header on the first request for each test context,
# then forwards subsequent requests to git-http-backend. Each test context
# is isolated, allowing multiple tests to run independently.

test_expect_success 'HTTP 429 with retries disabled (maxRetries=0) fails immediately' '
	# Set maxRetries to 0 (disabled)
	test_config http.maxRetries 0 &&
	test_config http.retryAfter 1 &&

	# Should fail immediately without any retry attempt
	test_must_fail git ls-remote "$HTTPD_URL/http_429/retries-disabled/1/repo.git" 2>err &&

	# Verify no retry happened (no "waiting" message in stderr)
	test_grep ! -i "waiting.*retry" err
'

test_expect_success 'HTTP 429 permanent should fail after max retries' '
	# Enable retries with a limit
	test_config http.maxRetries 2 &&

	# Git should retry but eventually fail when 429 persists
	test_must_fail git ls-remote "$HTTPD_URL/http_429/permanent-fail/permanent/repo.git" 2>err
'

test_expect_success 'HTTP 429 with Retry-After is retried and succeeds' '
	# Enable retries
	test_config http.maxRetries 3 &&

	# Git should retry after receiving 429 and eventually succeed
	git ls-remote "$HTTPD_URL/http_429/retry-succeeds/1/repo.git" >output 2>err &&
	test_grep "refs/heads/" output
'

test_expect_success 'http.maxRetries=1 retries once and succeeds' '
	test_config http.maxRetries 1 &&
	git ls-remote "$HTTPD_URL/http_429/max-retries-one/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output
'

test_expect_success 'http.maxRetries=2 fails after 3 failures and succeeds on 2 failures' '
	test_config http.maxRetries 2 &&
	test_must_fail git ls-remote "$HTTPD_URL/http_429/max-retries-two-fail-3/none/repo.git" 2>err &&
	test_grep "rate limited" err &&
	git ls-remote "$HTTPD_URL/http_429/max-retries-two-fail-2/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output
'

test_expect_success 'Trace2 logs actual CURLINFO_RETRY_AFTER value instead of -1' '
	test_config http.maxRetries 1 &&
	GIT_TRACE2_EVENT="$(pwd)/trace-retry-after.event" \
		git ls-remote "$HTTPD_URL/http_429/trace2-retry-after/1/repo.git" >output 2>err &&
	test_grep "\"key\":\"http/429-retry-after\",\"value\":\"1\"" trace-retry-after.event
'

test_expect_success 'HTTP 429 without Retry-After uses configured default' '
	# Enable retries and configure default delay
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&

	# Git should retry using configured default and succeed
	git ls-remote "$HTTPD_URL/http_429/no-retry-after-header/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output
'

test_expect_success 'exponential backoff doubles http.retryAfter without Retry-After header' '
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&
	git ls-remote "$HTTPD_URL/http_429/exp-backoff-fail-3/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "waiting 1 seconds before retry" err &&
	test_grep "waiting 2 seconds before retry" err &&
	test_grep "waiting 4 seconds before retry" err
'

test_expect_success 'exponential backoff clamps delay at http.maxRetryTime' '
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&
	test_config http.maxRetryTime 2 &&
	GIT_TRACE2_EVENT="$(pwd)/trace-clamp.event" \
		git ls-remote "$HTTPD_URL/http_429/exp-clamp-fail-3/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "waiting 1 seconds before retry" err &&
	test_grep "waiting 2 seconds before retry" err &&
	test_grep ! "waiting 4 seconds before retry" err &&
	grep "\"key\":\"http/retry-sleep-seconds\",\"value\":\"2\"" trace-clamp.event >clamped &&
	test_line_count = 2 clamped
'

test_expect_success 'server Retry-After header overrides exponential backoff' '
	test_config http.maxRetries 2 &&
	test_config http.retryAfter 1 &&
	git ls-remote "$HTTPD_URL/http_429/header-override-fail-2/1/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "waiting 1 seconds before retry" err &&
	test_grep ! "waiting 2 seconds before retry" err
'

test_expect_success 'HTTP 429 retry delays are respected' '
	# Enable retries
	test_config http.maxRetries 3 &&

	# Time the operation - it should take at least 2 seconds due to retry delay
	start=$(test-tool date getnanos) &&
	git ls-remote "$HTTPD_URL/http_429/retry-delays-respected/2/repo.git" >output 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Verify it took at least 2 seconds (allowing some tolerance)
	duration_int=${duration%.*} &&
	test "$duration_int" -ge 1 &&
	test_grep "refs/heads/" output
'

test_expect_success 'HTTP 429 fails immediately if Retry-After exceeds http.maxRetryTime' '
	# Configure max retry time to 3 seconds (much less than requested 100)
	test_config http.maxRetries 3 &&
	test_config http.maxRetryTime 3 &&

	# Should fail immediately without waiting
	start=$(test-tool date getnanos) &&
	test_must_fail git ls-remote "$HTTPD_URL/http_429/retry-after-exceeds-max-time/100/repo.git" 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should fail quickly (no 100 second wait)
	duration_int=${duration%.*} &&
	test "$duration_int" -lt 99 &&
	test_grep "greater than http.maxRetryTime" err
'

test_expect_success 'HTTP 429 fails if configured http.retryAfter exceeds http.maxRetryTime' '
	# Test misconfiguration: retryAfter > maxRetryTime
	# Configure retryAfter larger than maxRetryTime
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 100 &&
	test_config http.maxRetryTime 5 &&

	# Should fail immediately with configuration error
	start=$(test-tool date getnanos) &&
	test_must_fail git ls-remote "$HTTPD_URL/http_429/config-retry-after-exceeds-max-time/none/repo.git" 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should fail quickly (no 100 second wait)
	duration_int=${duration%.*} &&
	test "$duration_int" -lt 99 &&
	test_grep "configured http.retryAfter.*exceeds.*http.maxRetryTime" err
'

test_expect_success 'HTTP 429 with Retry-After HTTP-date format' '
	# Test HTTP-date format (RFC 2822) in Retry-After header
	raw=$(test-tool date timestamp now) &&
	now="${raw#* -> }" &&
	future_time=$((now + 2)) &&
	raw=$(test-tool date show:rfc2822 $future_time) &&
	future_date="${raw#* -> }" &&
	future_date_encoded=$(echo "$future_date" | sed "s/ /%20/g") &&

	# Enable retries
	test_config http.maxRetries 3 &&

	# Git should parse the HTTP-date and retry after the delay
	start=$(test-tool date getnanos) &&
	git ls-remote "$HTTPD_URL/http_429/http-date-format/$future_date_encoded/repo.git" >output 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should take at least 1 second (allowing tolerance for processing time)
	duration_int=${duration%.*} &&
	test "$duration_int" -ge 1 &&
	test_grep "refs/heads/" output
'

test_expect_success 'HTTP 429 with HTTP-date exceeding maxRetryTime fails immediately' '
	raw=$(test-tool date timestamp now) &&
	now="${raw#* -> }" &&
	future_time=$((now + 200)) &&
	raw=$(test-tool date show:rfc2822 $future_time) &&
	future_date="${raw#* -> }" &&
	future_date_encoded=$(echo "$future_date" | sed "s/ /%20/g") &&

	# Configure max retry time much less than the 200 second delay
	test_config http.maxRetries 3 &&
	test_config http.maxRetryTime 10 &&

	# Should fail immediately without waiting 200 seconds
	start=$(test-tool date getnanos) &&
	test_must_fail git ls-remote "$HTTPD_URL/http_429/http-date-exceeds-max-time/$future_date_encoded/repo.git" 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should fail quickly (not wait 200 seconds)
	duration_int=${duration%.*} &&
	test "$duration_int" -lt 199 &&
	test_grep "http.maxRetryTime" err
'

test_expect_success 'HTTP 429 with past HTTP-date should not wait' '
	raw=$(test-tool date timestamp now) &&
	now="${raw#* -> }" &&
	past_time=$((now - 10)) &&
	raw=$(test-tool date show:rfc2822 $past_time) &&
	past_date="${raw#* -> }" &&
	past_date_encoded=$(echo "$past_date" | sed "s/ /%20/g") &&

	# Enable retries
	test_config http.maxRetries 3 &&

	# Git should retry immediately without waiting
	start=$(test-tool date getnanos) &&
	git ls-remote "$HTTPD_URL/http_429/past-http-date/$past_date_encoded/repo.git" >output 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should complete quickly (no wait for a past-date Retry-After)
	duration_int=${duration%.*} &&
	test "$duration_int" -lt 5 &&
	test_grep "refs/heads/" output
'

test_expect_success 'HTTP 429 with invalid Retry-After format uses configured default' '
	# Configure default retry-after
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&

	# Should use configured default (1 second) since header is invalid
	start=$(test-tool date getnanos) &&
	git ls-remote "$HTTPD_URL/http_429/invalid-retry-after-format/invalid/repo.git" >output 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should take at least 1 second (the configured default)
	duration_int=${duration%.*} &&
	test "$duration_int" -ge 1 &&
	test_grep "refs/heads/" output &&
	test_grep "waiting.*retry" err
'

test_expect_success 'HTTP 429 will not be retried without config' '
	# Default config means http.maxRetries=0 (retries disabled)
	# When 429 is received, it should fail immediately without retry
	# Do NOT configure anything - use defaults (http.maxRetries defaults to 0)

	# Should fail immediately without retry
	test_must_fail git ls-remote "$HTTPD_URL/http_429/no-retry-without-config/1/repo.git" 2>err &&

	# Verify no retry happened (no "waiting" message)
	test_grep ! -i "waiting.*retry" err &&

	# Should get 429 error
	test_grep "429" err
'

test_expect_success 'GIT_HTTP_RETRY_AFTER overrides http.retryAfter config' '
	# Configure retryAfter to 10 seconds
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 10 &&

	# Override with environment variable to 1 second
	start=$(test-tool date getnanos) &&
	GIT_HTTP_RETRY_AFTER=1 git ls-remote "$HTTPD_URL/http_429/env-retry-after-override/none/repo.git" >output 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should use env var (1 second), not config (10 seconds)
	duration_int=${duration%.*} &&
	test "$duration_int" -ge 1 &&
	test "$duration_int" -lt 5 &&
	test_grep "refs/heads/" output &&
	test_grep "waiting.*retry" err
'

test_expect_success 'GIT_HTTP_MAX_RETRIES overrides http.maxRetries config' '
	# Configure maxRetries to 0 (disabled)
	test_config http.maxRetries 0 &&
	test_config http.retryAfter 1 &&

	# Override with environment variable to enable retries
	GIT_HTTP_MAX_RETRIES=3 git ls-remote "$HTTPD_URL/http_429/env-max-retries-override/1/repo.git" >output 2>err &&

	# Should retry (env var enables it despite config saying disabled)
	test_grep "refs/heads/" output &&
	test_grep "waiting.*retry" err
'

test_expect_success 'GIT_HTTP_MAX_RETRY_TIME overrides http.maxRetryTime config' '
	# Configure maxRetryTime to 100 seconds (would accept 50 second delay)
	test_config http.maxRetries 3 &&
	test_config http.maxRetryTime 100 &&

	# Override with environment variable to 10 seconds (should reject 50 second delay)
	start=$(test-tool date getnanos) &&
	test_must_fail env GIT_HTTP_MAX_RETRY_TIME=10 \
		git ls-remote "$HTTPD_URL/http_429/env-max-retry-time-override/50/repo.git" 2>err &&
	duration=$(test-tool date getnanos $start) &&

	# Should fail quickly (not wait 50 seconds) because env var limits to 10
	duration_int=${duration%.*} &&
	test "$duration_int" -lt 49 &&
	test_grep "greater than http.maxRetryTime" err
'

test_expect_success 'verify normal repository access still works' '
	git ls-remote "$HTTPD_URL/smart/repo.git" >output &&
	test_grep "refs/heads/" output
'

test_expect_success 'HTTP 502/503/504 are not retried by default' '
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&
	test_must_fail git ls-remote "$HTTPD_URL/http_429/default-no-retry-status-502/none/repo.git" 2>err &&
	test_grep ! -i "waiting.*retry" err &&
	test_grep "The requested URL returned error: 502" err
'

test_expect_success 'http.retryStatusCodes retries configured 502, 503, and 504 errors' '
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&
	test_config http.retryStatusCodes "429, 502, 503, 504" &&
	git ls-remote "$HTTPD_URL/http_429/retry-status-502/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "HTTP 502, waiting 1 seconds before retry" err &&
	test_config http.retryAfter 0 &&
	git ls-remote "$HTTPD_URL/http_429/retry-status-503/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	git ls-remote "$HTTPD_URL/http_429/retry-status-504/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output
'

test_expect_success 'HTTP 503 honors Retry-After header when present' '
	test_config http.maxRetries 2 &&
	test_config http.retryStatusCodes "429,503" &&
	git ls-remote "$HTTPD_URL/http_429/header-status-503/1/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "HTTP 503, waiting 1 seconds before retry" err
'

test_expect_success 'exhausted 502 retries report HTTP 502 error, not rate limited' '
	test_config http.maxRetries 2 &&
	test_config http.retryStatusCodes "429,502" &&
	test_must_fail git ls-remote "$HTTPD_URL/http_429/exhausted-fail-3-status-502/none/repo.git" 2>err &&
	test_grep "The requested URL returned error: 502" err &&
	test_grep ! "rate limited" err
'

test_expect_success 'GIT_HTTP_RETRY_STATUS_CODES and urlmatch http.<url>.retryStatusCodes' '
	test_config http.maxRetries 2 &&
	GIT_HTTP_RETRY_STATUS_CODES="429,502" \
		git ls-remote "$HTTPD_URL/http_429/env-status-502/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_config "http.$HTTPD_URL.retryStatusCodes" "429,503" &&
	git ls-remote "$HTTPD_URL/http_429/urlmatch-status-503/none/repo.git" >output 2>err &&
	test_grep "refs/heads/" output
'

test_expect_success 'Protocol v2 POST /git-upload-pack retries 429 and 502' '
	test_when_finished "rm -rf clone-v2-429 clone-v2-502 clone-v2-fail" &&
	test_config_global protocol.version 2 &&
	test_config_global http.maxRetries 3 &&
	test_config_global http.retryAfter 1 &&
	test_config_global http.retryStatusCodes "429,502" &&
	git clone "$HTTPD_URL/http_429/v2-post-only-429/1/repo.git" clone-v2-429 2>err &&
	test_grep "rate limited, waiting 1 seconds before retry" err &&
	test_grep ! "RPC failed" err &&
	git clone "$HTTPD_URL/http_429/v2-post-only-status-502/1/repo.git" clone-v2-502 2>err &&
	test_grep "HTTP 502, waiting 1 seconds before retry" err &&
	test_grep ! "RPC failed" err &&
	test_config_global http.maxRetries 1 &&
	test_config_global http.retryAfter 0 &&
	test_must_fail git clone \
		"$HTTPD_URL/http_429/v2-post-only-fail-2-status-502/none/repo.git" \
		clone-v2-fail 2>err &&
	test_grep "RPC failed; HTTP 502" err
'

test_expect_success 'partial clone lazy blob fetch (promisor-remote) retries transient 502' '
	test_when_finished "rm -rf partial" &&
	test_config -C "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" uploadpack.allowfilter 1 &&
	test_config -C "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" uploadpack.allowanysha1inwant 1 &&
	git clone --filter=blob:none --no-checkout "$HTTPD_URL/smart/repo.git" partial &&
	git -C partial config remote.origin.url \
		"$HTTPD_URL/http_429/promisor-post-only-status-502/1/repo.git" &&
	git -C partial config http.maxRetries 3 &&
	git -C partial config http.retryAfter 1 &&
	git -C partial config http.retryStatusCodes "429,502" &&
	git -C partial checkout HEAD 2>err &&
	test_grep "HTTP 502, waiting 1 seconds before retry" err &&
	test_grep ! "RPC failed" err
'

test_expect_success 'gzipped POST /git-upload-pack preserves Content-Encoding: gzip on retry' '
	test_when_finished "rm -f curl.log gzip.headers" &&
	test_config protocol.version 2 &&
	test_config http.maxRetries 3 &&
	test_config http.retryAfter 1 &&
	set -- &&
	for i in $(test_seq 30)
	do
		set -- "$@" -o "very-long-server-option-to-exceed-1024-bytes-$i" || return 1
	done &&
	GIT_TRACE_CURL="$(pwd)/curl.log" git ls-remote "$@" \
		"$HTTPD_URL/http_429/gzip-post-only-429/1/repo.git" >output 2>err &&
	test_grep "refs/heads/" output &&
	test_grep "rate limited, waiting 1 seconds before retry" err &&
	grep "Content-Encoding: gzip" curl.log >gzip.headers &&
	test_line_count = 2 gzip.headers
'

test_done
