#!/bin/sh

test_description='simple command server'

. ./test-lib.sh

test-tool simple-ipc SUPPORTS_SIMPLE_IPC || {
	skip_all='simple IPC not supported on this platform'
	test_done
}

stop_simple_IPC_server () {
	test-tool simple-ipc stop-daemon
}

test_expect_success 'start simple command server' '
	test_atexit stop_simple_IPC_server &&
	test-tool simple-ipc start-daemon --threads=8 &&
	test-tool simple-ipc is-active
'

test_expect_success 'simple command server' '
	test-tool simple-ipc send --token=ping >actual &&
	echo pong >expect &&
	test_cmp expect actual
'

test_expect_success 'servers cannot share the same path' '
	test_must_fail test-tool simple-ipc run-daemon &&
	test-tool simple-ipc is-active
'

test_expect_success 'big response' '
	test-tool simple-ipc send --token=big >actual &&
	test_line_count -ge 10000 actual &&
	grep -q "big: [0]*9999\$" actual
'

test_expect_success 'chunk response' '
	test-tool simple-ipc send --token=chunk >actual &&
	test_line_count -ge 10000 actual &&
	grep -q "big: [0]*9999\$" actual
'

test_expect_success 'slow response' '
	test-tool simple-ipc send --token=slow >actual &&
	test_line_count -ge 100 actual &&
	grep -q "big: [0]*99\$" actual
'

# Send an IPC with n=100,000 bytes of ballast.  This should be large enough
# to force both the kernel and the pkt-line layer to chunk the message to the
# daemon and for the daemon to receive it in chunks.
#
test_expect_success 'sendbytes' '
	test-tool simple-ipc sendbytes --bytecount=100000 --byte=A >actual &&
	grep "sent:A00100000 rcvd:A00100000" actual
'

# Start a series of <threads> client threads that each make <batchsize>
# IPC requests to the server.  Each (<threads> * <batchsize>) request
# will open a new connection to the server and randomly bind to a server
# thread.  Each client thread exits after completing its batch.  So the
# total number of live client threads will be smaller than the total.
# Each request will send a message containing at least <bytecount> bytes
# of ballast.  (Responses are small.)
#
# The purpose here is to test threading in the server and responding to
# many concurrent client requests (regardless of whether they come from
# 1 client process or many).  And to test that the server side of the
# named pipe/socket is stable.  (On Windows this means that the server
# pipe is properly recycled.)
#
# On Windows it also lets us adjust the connection timeout in the
# `ipc_client_send_command()`.
#
# Note it is easy to drive the system into failure by requesting an
# insane number of threads on client or server and/or increasing the
# per-thread batchsize or the per-request bytecount (ballast).
# On Windows these failures look like "pipe is busy" errors.
# So I've chosen fairly conservative values for now.
#
# We expect output of the form "sent:<letter><length> ..."
# With terms (7, 19, 13) we expect:
#   <letter> in [A-G]
#   <length> in [19+0 .. 19+(13-1)]
# and (7 * 13) successful responses.
#
test_expect_success 'stress test threads' '
	test-tool simple-ipc multiple \
		--threads=7 \
		--bytecount=19 \
		--batchsize=13 \
		>actual &&
	test_line_count = 92 actual &&
	grep "good 91" actual &&
	grep "sent:A" <actual >actual_a &&
	cat >expect_a <<-EOF &&
		sent:A00000019 rcvd:A00000019
		sent:A00000020 rcvd:A00000020
		sent:A00000021 rcvd:A00000021
		sent:A00000022 rcvd:A00000022
		sent:A00000023 rcvd:A00000023
		sent:A00000024 rcvd:A00000024
		sent:A00000025 rcvd:A00000025
		sent:A00000026 rcvd:A00000026
		sent:A00000027 rcvd:A00000027
		sent:A00000028 rcvd:A00000028
		sent:A00000029 rcvd:A00000029
		sent:A00000030 rcvd:A00000030
		sent:A00000031 rcvd:A00000031
	EOF
	test_cmp expect_a actual_a
'

test_expect_success 'stop-daemon works' '
	test-tool simple-ipc stop-daemon &&
	test_must_fail test-tool simple-ipc is-active &&
	test_must_fail test-tool simple-ipc send --token=ping
'

test_done
