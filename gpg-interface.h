#ifndef GPG_INTERFACE_H
#define GPG_INTERFACE_H

struct strbuf;

#define GPG_VERIFY_VERBOSE		1
#define GPG_VERIFY_RAW			2
#define GPG_VERIFY_OMIT_STATUS	4

enum signature_trust_level {
	TRUST_UNDEFINED,
	TRUST_NEVER,
	TRUST_MARGINAL,
	TRUST_FULLY,
	TRUST_ULTIMATE,
};

enum payload_type {
	SIGNATURE_PAYLOAD_UNDEFINED,
	SIGNATURE_PAYLOAD_COMMIT,
	SIGNATURE_PAYLOAD_TAG,
	SIGNATURE_PAYLOAD_PUSH_CERT,
};

struct signature_check {
	char *payload;
	size_t payload_len;
	enum payload_type payload_type;
	timestamp_t payload_timestamp;
	char *output;
	char *gpg_status;

	/*
	 * possible "result":
	 * 0 (not checked)
	 * N (checked but no further result)
	 * G (good)
	 * B (bad)
	 */
	char result;
	char *signer;
	char *key;
	char *fingerprint;
	char *primary_key_fingerprint;
	enum signature_trust_level trust_level;
};

void signature_check_clear(struct signature_check *sigc);

/*
 * Look at a GPG signed tag object.  If such a signature exists, store it in
 * signature and the signed content in payload.  Return 1 if a signature was
 * found, and 0 otherwise.
 */
int parse_signature(const char *buf, size_t size, struct strbuf *payload, struct strbuf *signature);

/*
 * Look at GPG signed content (e.g. a signed tag object), whose
 * payload is followed by a detached signature on it.  Return the
 * offset where the embedded detached signature begins, or the end of
 * the data when there is no such signature.
 */
size_t parse_signed_buffer(const char *buf, size_t size);

/*
 * Create a detached signature for the contents of "buffer" and append
 * it after "signature"; "buffer" and "signature" can be the same
 * strbuf instance, which would cause the detached signature appended
 * at the end.  Returns 0 on success, non-zero on failure.
 */
int sign_buffer(struct strbuf *buffer, struct strbuf *signature,
		const char *signing_key);


/*
 * Returns corresponding string in lowercase for a given member of
 * enum signature_trust_level. For example, `TRUST_ULTIMATE` will
 * return "ultimate".
 */
const char *gpg_trust_level_to_str(enum signature_trust_level level);

void set_signing_key(const char *);
char *get_signing_key(void);

/*
 * Returns a textual unique representation of the signing key in use
 * Either a GPG KeyID or a SSH Key Fingerprint
 */
char *get_signing_key_id(void);
int check_signature(struct signature_check *sigc,
		    const char *signature, size_t slen);
void print_signature_buffer(const struct signature_check *sigc,
			    unsigned flags);

#endif
