#ifndef GPG_INTERFACE_H
#define GPG_INTERFACE_H

#define GPG_VERIFY_VERBOSE	1
#define GPG_VERIFY_RAW		2

struct signature_check {
	char *payload;
	char *gpg_output;
	char *gpg_status;

	/*
	 * possible "result":
	 * 0 (not checked)
	 * N (checked but no further result)
	 * U (untrusted good)
	 * G (good)
	 * B (bad)
	 */
	char result;
	char *signer;
	char *key;
};

extern void signature_check_clear(struct signature_check *sigc);
extern size_t parse_signature(const char *buf, unsigned long size);
extern void parse_gpg_output(struct signature_check *);
extern int sign_buffer(struct strbuf *buffer, struct strbuf *signature, const char *signing_key);
extern int verify_signed_buffer(const char *payload, size_t payload_size, const char *signature, size_t signature_size, struct strbuf *gpg_output, struct strbuf *gpg_status);
extern int git_gpg_config(const char *, const char *, void *);
extern void set_signing_key(const char *);
extern const char *get_signing_key(void);
extern int check_signature(const char *payload, size_t plen,
	const char *signature, size_t slen, struct signature_check *sigc);
void print_signature_buffer(const struct signature_check *sigc, unsigned flags);

#endif
