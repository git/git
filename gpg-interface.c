#include "cache.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "gpg-interface.h"
#include "sigchain.h"
#include "tempfile.h"

static char *configured_signing_key;
static const char *gpg_program = "gpg";

#define PGP_SIGNATURE "-----BEGIN PGP SIGNATURE-----"
#define PGP_MESSAGE "-----BEGIN PGP MESSAGE-----"

void signature_check_clear(struct signature_check *sigc)
{
	FREE_AND_NULL(sigc->payload);
	FREE_AND_NULL(sigc->gpg_output);
	FREE_AND_NULL(sigc->gpg_status);
	FREE_AND_NULL(sigc->signer);
	FREE_AND_NULL(sigc->key);
}

static struct {
	char result;
	const char *check;
} sigcheck_gpg_status[] = {
	{ 'G', "\n[GNUPG:] GOODSIG " },
	{ 'B', "\n[GNUPG:] BADSIG " },
	{ 'U', "\n[GNUPG:] TRUST_NEVER" },
	{ 'U', "\n[GNUPG:] TRUST_UNDEFINED" },
	{ 'E', "\n[GNUPG:] ERRSIG "},
	{ 'X', "\n[GNUPG:] EXPSIG "},
	{ 'Y', "\n[GNUPG:] EXPKEYSIG "},
	{ 'R', "\n[GNUPG:] REVKEYSIG "},
};

void parse_gpg_output(struct signature_check *sigc)
{
	const char *buf = sigc->gpg_status;
	int i;

	/* Iterate over all search strings */
	for (i = 0; i < ARRAY_SIZE(sigcheck_gpg_status); i++) {
		const char *found, *next;

		if (!skip_prefix(buf, sigcheck_gpg_status[i].check + 1, &found)) {
			found = strstr(buf, sigcheck_gpg_status[i].check);
			if (!found)
				continue;
			found += strlen(sigcheck_gpg_status[i].check);
		}
		sigc->result = sigcheck_gpg_status[i].result;
		/* The trust messages are not followed by key/signer information */
		if (sigc->result != 'U') {
			sigc->key = xmemdupz(found, 16);
			/* The ERRSIG message is not followed by signer information */
			if (sigc-> result != 'E') {
				found += 17;
				next = strchrnul(found, '\n');
				sigc->signer = xmemdupz(found, next - found);
			}
		}
	}
}

int check_signature(const char *payload, size_t plen, const char *signature,
	size_t slen, struct signature_check *sigc)
{
	struct strbuf gpg_output = STRBUF_INIT;
	struct strbuf gpg_status = STRBUF_INIT;
	int status;

	sigc->result = 'N';

	status = verify_signed_buffer(payload, plen, signature, slen,
				      &gpg_output, &gpg_status);
	if (status && !gpg_output.len)
		goto out;
	sigc->payload = xmemdupz(payload, plen);
	sigc->gpg_output = strbuf_detach(&gpg_output, NULL);
	sigc->gpg_status = strbuf_detach(&gpg_status, NULL);
	parse_gpg_output(sigc);

 out:
	strbuf_release(&gpg_status);
	strbuf_release(&gpg_output);

	return sigc->result != 'G' && sigc->result != 'U';
}

void print_signature_buffer(const struct signature_check *sigc, unsigned flags)
{
	const char *output = flags & GPG_VERIFY_RAW ?
		sigc->gpg_status : sigc->gpg_output;

	if (flags & GPG_VERIFY_VERBOSE && sigc->payload)
		fputs(sigc->payload, stdout);

	if (output)
		fputs(output, stderr);
}

/*
 * Look at GPG signed content (e.g. a signed tag object), whose
 * payload is followed by a detached signature on it.  Return the
 * offset where the embedded detached signature begins, or the end of
 * the data when there is no such signature.
 */
size_t parse_signature(const char *buf, unsigned long size)
{
	char *eol;
	size_t len = 0;
	while (len < size && !starts_with(buf + len, PGP_SIGNATURE) &&
			!starts_with(buf + len, PGP_MESSAGE)) {
		eol = memchr(buf + len, '\n', size - len);
		len += eol ? eol - (buf + len) + 1 : size - len;
	}
	return len;
}

void set_signing_key(const char *key)
{
	free(configured_signing_key);
	configured_signing_key = xstrdup(key);
}

int git_gpg_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "user.signingkey")) {
		set_signing_key(value);
	}
	if (!strcmp(var, "gpg.program")) {
		if (!value)
			return config_error_nonbool(var);
		gpg_program = xstrdup(value);
	}
	return 0;
}

const char *get_signing_key(void)
{
	if (configured_signing_key)
		return configured_signing_key;
	return git_committer_info(IDENT_STRICT|IDENT_NO_DATE);
}

/*
 * Create a detached signature for the contents of "buffer" and append
 * it after "signature"; "buffer" and "signature" can be the same
 * strbuf instance, which would cause the detached signature appended
 * at the end.
 */
int sign_buffer(struct strbuf *buffer, struct strbuf *signature, const char *signing_key)
{
	struct child_process gpg = CHILD_PROCESS_INIT;
	int ret;
	size_t i, j, bottom;

	argv_array_pushl(&gpg.args,
			 gpg_program,
			 "-bsau", signing_key,
			 NULL);

	bottom = signature->len;

	/*
	 * When the username signingkey is bad, program could be terminated
	 * because gpg exits without reading and then write gets SIGPIPE.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&gpg, buffer->buf, buffer->len,
			   signature, 1024, NULL, 0);
	sigchain_pop(SIGPIPE);

	if (ret || signature->len == bottom)
		return error(_("gpg failed to sign the data"));

	/* Strip CR from the line endings, in case we are on Windows. */
	for (i = j = bottom; i < signature->len; i++)
		if (signature->buf[i] != '\r') {
			if (i != j)
				signature->buf[j] = signature->buf[i];
			j++;
		}
	strbuf_setlen(signature, j);

	return 0;
}

/*
 * Run "gpg" to see if the payload matches the detached signature.
 * gpg_output, when set, receives the diagnostic output from GPG.
 * gpg_status, when set, receives the status output from GPG.
 */
int verify_signed_buffer(const char *payload, size_t payload_size,
			 const char *signature, size_t signature_size,
			 struct strbuf *gpg_output, struct strbuf *gpg_status)
{
	struct child_process gpg = CHILD_PROCESS_INIT;
	static struct tempfile temp;
	int fd, ret;
	struct strbuf buf = STRBUF_INIT;

	fd = mks_tempfile_t(&temp, ".git_vtag_tmpXXXXXX");
	if (fd < 0)
		return error_errno(_("could not create temporary file"));
	if (write_in_full(fd, signature, signature_size) < 0) {
		error_errno(_("failed writing detached signature to '%s'"),
			    temp.filename.buf);
		delete_tempfile(&temp);
		return -1;
	}
	close(fd);

	argv_array_pushl(&gpg.args,
			 gpg_program,
			 "--status-fd=1",
			 "--keyid-format=long",
			 "--verify", temp.filename.buf, "-",
			 NULL);

	if (!gpg_status)
		gpg_status = &buf;

	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&gpg, payload, payload_size,
			   gpg_status, 0, gpg_output, 0);
	sigchain_pop(SIGPIPE);

	delete_tempfile(&temp);

	ret |= !strstr(gpg_status->buf, "\n[GNUPG:] GOODSIG ");
	strbuf_release(&buf); /* no matter it was used or not */

	return ret;
}
