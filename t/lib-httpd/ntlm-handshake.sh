#!/bin/sh

case "$HTTP_AUTHORIZATION" in
'')
	# No Authorization header -> send NTLM challenge
	echo "Status: 401 Unauthorized"
	echo "WWW-Authenticate: NTLM"
	echo
	;;
"NTLM TlRMTVNTUAAB"*)
	# Type 1 -> respond with Type 2 challenge (hardcoded)
	echo "Status: 401 Unauthorized"
	# Base64-encoded version of the Type 2 challenge:
	# signature: 'NTLMSSP\0'
	# message_type: 2
	# target_name: 'NTLM-GIT-SERVER'
	# flags: 0xa2898205 =
	#   NEGOTIATE_UNICODE, REQUEST_TARGET, NEGOTIATE_NT_ONLY,
	#   TARGET_TYPE_SERVER, TARGET_TYPE_SHARE, REQUEST_NON_NT_SESSION_KEY,
	#   NEGOTIATE_VERSION, NEGOTIATE_128, NEGOTIATE_56
	# challenge: 0xfa3dec518896295b
	# context: '0000000000000000'
	# target_info_present: true
	# target_info_len: 128
	# version: '10.0 (build 19041)'
	echo "WWW-Authenticate: NTLM TlRMTVNTUAACAAAAHgAeADgAAAAFgomi+j3sUYiWKVsAAAAAAAAAAIAAgABWAAAACgBhSgAAAA9OAFQATABNAC0ARwBJAFQALQBTAEUAUgBWAEUAUgACABIAVwBPAFIASwBHAFIATwBVAFAAAQAeAE4AVABMAE0ALQBHAEkAVAAtAFMARQBSAFYARQBSAAQAEgBXAE8AUgBLAEcAUgBPAFUAUAADAB4ATgBUAEwATQAtAEcASQBUAC0AUwBFAFIAVgBFAFIABwAIAACfOcZKYNwBAAAAAA=="
	echo
	;;
"NTLM TlRMTVNTUAAD"*)
	# Type 3 -> accept without validation
	exec "$GIT_EXEC_PATH"/git-http-backend
	;;
*)
	echo "Status: 500 Unrecognized"
	echo
	echo "Unhandled auth: '$HTTP_AUTHORIZATION'"
	;;
esac
