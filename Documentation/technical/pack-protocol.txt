Pack transfer protocols
=======================

There are two Pack push-pull protocols.

upload-pack (S) | fetch/clone-pack (C) protocol:

	# Tell the puller what commits we have and what their names are
	S: SHA1 name
	S: ...
	S: SHA1 name
	S: # flush -- it's your turn
	# Tell the pusher what commits we want, and what we have
	C: want name
	C: ..
	C: want name
	C: have SHA1
	C: have SHA1
	C: ...
	C: # flush -- occasionally ask "had enough?"
	S: NAK
	C: have SHA1
	C: ...
	C: have SHA1
	S: ACK
	C: done
	S: XXXXXXX -- packfile contents.

send-pack | receive-pack protocol.

	# Tell the pusher what commits we have and what their names are
	C: SHA1 name
	C: ...
	C: SHA1 name
	C: # flush -- it's your turn
	# Tell the puller what the pusher has
	S: old-SHA1 new-SHA1 name
	S: old-SHA1 new-SHA1 name
	S: ...
	S: # flush -- done with the list
	S: XXXXXXX --- packfile contents.
