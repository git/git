#include "git-compat-util.h"
#include "commit.h"
#include "config.h"
#include "date.h"
#include "gettext.h"
#include "run-command.h"
#include "strbuf.h"
#include "dir.h"
#include "ident.h"
#include "gpg-interface.h"
#include "path.h"
#include "sigchain.h"
#include "tempfile.h"
#include "alias.h"
#include "wrapper.h"
#include "environment.h"

static int git_gpg_config(const char *, const char *,
			  const struct config_context *, void *);

static void gpg_interface_lazy_init(void)
{
	static int done;

	if (done)
		return;
	done = 1;
	git_config(git_gpg_config, NULL);
}

static char *configured_signing_key;
static const char *ssh_default_key_command, *ssh_allowed_signers, *ssh_revocation_file;
static enum signature_trust_level configured_min_trust_level = TRUST_UNDEFINED;

struct gpg_format {
	const char *name;
	const char *program;
	const char **verify_args;
	const char **sigs;
	int (*verify_signed_buffer)(struct signature_check *sigc,
				    struct gpg_format *fmt,
				    const char *signature,
				    size_t signature_size);
	int (*sign_buffer)(struct strbuf *buffer, struct strbuf *signature,
			   const char *signing_key);
	const char *(*get_default_key)(void);
	const char *(*get_key_id)(void);
};

static const char *openpgp_verify_args[] = {
	"--keyid-format=long",
	NULL
};
static const char *openpgp_sigs[] = {
	"-----BEGIN PGP SIGNATURE-----",
	"-----BEGIN PGP MESSAGE-----",
	NULL
};

static const char *x509_verify_args[] = {
	NULL
};
static const char *x509_sigs[] = {
	"-----BEGIN SIGNED MESSAGE-----",
	NULL
};

static const char *ssh_verify_args[] = { NULL };
static const char *ssh_sigs[] = {
	"-----BEGIN SSH SIGNATURE-----",
	NULL
};

static int verify_gpg_signed_buffer(struct signature_check *sigc,
				    struct gpg_format *fmt,
				    const char *signature,
				    size_t signature_size);
static int verify_ssh_signed_buffer(struct signature_check *sigc,
				    struct gpg_format *fmt,
				    const char *signature,
				    size_t signature_size);
static int sign_buffer_gpg(struct strbuf *buffer, struct strbuf *signature,
			   const char *signing_key);
static int sign_buffer_ssh(struct strbuf *buffer, struct strbuf *signature,
			   const char *signing_key);

static const char *get_default_ssh_signing_key(void);

static const char *get_ssh_key_id(void);

static struct gpg_format gpg_format[] = {
	{
		.name = "openpgp",
		.program = "gpg",
		.verify_args = openpgp_verify_args,
		.sigs = openpgp_sigs,
		.verify_signed_buffer = verify_gpg_signed_buffer,
		.sign_buffer = sign_buffer_gpg,
		.get_default_key = NULL,
		.get_key_id = NULL,
	},
	{
		.name = "x509",
		.program = "gpgsm",
		.verify_args = x509_verify_args,
		.sigs = x509_sigs,
		.verify_signed_buffer = verify_gpg_signed_buffer,
		.sign_buffer = sign_buffer_gpg,
		.get_default_key = NULL,
		.get_key_id = NULL,
	},
	{
		.name = "ssh",
		.program = "ssh-keygen",
		.verify_args = ssh_verify_args,
		.sigs = ssh_sigs,
		.verify_signed_buffer = verify_ssh_signed_buffer,
		.sign_buffer = sign_buffer_ssh,
		.get_default_key = get_default_ssh_signing_key,
		.get_key_id = get_ssh_key_id,
	},
};

static struct gpg_format *use_format = &gpg_format[0];

static struct gpg_format *get_format_by_name(const char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gpg_format); i++)
		if (!strcmp(gpg_format[i].name, str))
			return gpg_format + i;
	return NULL;
}

static struct gpg_format *get_format_by_sig(const char *sig)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(gpg_format); i++)
		for (j = 0; gpg_format[i].sigs[j]; j++)
			if (starts_with(sig, gpg_format[i].sigs[j]))
				return gpg_format + i;
	return NULL;
}

void signature_check_clear(struct signature_check *sigc)
{
	FREE_AND_NULL(sigc->payload);
	FREE_AND_NULL(sigc->output);
	FREE_AND_NULL(sigc->gpg_status);
	FREE_AND_NULL(sigc->signer);
	FREE_AND_NULL(sigc->key);
	FREE_AND_NULL(sigc->fingerprint);
	FREE_AND_NULL(sigc->primary_key_fingerprint);
}

/* An exclusive status -- only one of them can appear in output */
#define GPG_STATUS_EXCLUSIVE	(1<<0)
/* The status includes key identifier */
#define GPG_STATUS_KEYID	(1<<1)
/* The status includes user identifier */
#define GPG_STATUS_UID		(1<<2)
/* The status includes key fingerprints */
#define GPG_STATUS_FINGERPRINT	(1<<3)
/* The status includes trust level */
#define GPG_STATUS_TRUST_LEVEL	(1<<4)

/* Short-hand for standard exclusive *SIG status with keyid & UID */
#define GPG_STATUS_STDSIG	(GPG_STATUS_EXCLUSIVE|GPG_STATUS_KEYID|GPG_STATUS_UID)

static struct {
	char result;
	const char *check;
	unsigned int flags;
} sigcheck_gpg_status[] = {
	{ 'G', "GOODSIG ", GPG_STATUS_STDSIG },
	{ 'B', "BADSIG ", GPG_STATUS_STDSIG },
	{ 'E', "ERRSIG ", GPG_STATUS_EXCLUSIVE|GPG_STATUS_KEYID },
	{ 'X', "EXPSIG ", GPG_STATUS_STDSIG },
	{ 'Y', "EXPKEYSIG ", GPG_STATUS_STDSIG },
	{ 'R', "REVKEYSIG ", GPG_STATUS_STDSIG },
	{ 0, "VALIDSIG ", GPG_STATUS_FINGERPRINT },
	{ 0, "TRUST_", GPG_STATUS_TRUST_LEVEL },
};

/* Keep the order same as enum signature_trust_level */
static struct sigcheck_gpg_trust_level {
	const char *key;
	const char *display_key;
	enum signature_trust_level value;
} sigcheck_gpg_trust_level[] = {
	{ "UNDEFINED", "undefined", TRUST_UNDEFINED },
	{ "NEVER", "never", TRUST_NEVER },
	{ "MARGINAL", "marginal", TRUST_MARGINAL },
	{ "FULLY", "fully", TRUST_FULLY },
	{ "ULTIMATE", "ultimate", TRUST_ULTIMATE },
};

static void replace_cstring(char **field, const char *line, const char *next)
{
	free(*field);

	if (line && next)
		*field = xmemdupz(line, next - line);
	else
		*field = NULL;
}

static int parse_gpg_trust_level(const char *level,
				 enum signature_trust_level *res)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sigcheck_gpg_trust_level); i++) {
		if (!strcmp(sigcheck_gpg_trust_level[i].key, level)) {
			*res = sigcheck_gpg_trust_level[i].value;
			return 0;
		}
	}
	return 1;
}

static void parse_gpg_output(struct signature_check *sigc)
{
	const char *buf = sigc->gpg_status;
	const char *line, *next;
	int i, j;
	int seen_exclusive_status = 0;

	/* Iterate over all lines */
	for (line = buf; *line; line = strchrnul(line+1, '\n')) {
		while (*line == '\n')
			line++;
		if (!*line)
			break;

		/* Skip lines that don't start with GNUPG status */
		if (!skip_prefix(line, "[GNUPG:] ", &line))
			continue;

		/* Iterate over all search strings */
		for (i = 0; i < ARRAY_SIZE(sigcheck_gpg_status); i++) {
			if (skip_prefix(line, sigcheck_gpg_status[i].check, &line)) {
				/*
				 * GOODSIG, BADSIG etc. can occur only once for
				 * each signature.  Therefore, if we had more
				 * than one then we're dealing with multiple
				 * signatures.  We don't support them
				 * currently, and they're rather hard to
				 * create, so something is likely fishy and we
				 * should reject them altogether.
				 */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_EXCLUSIVE) {
					if (seen_exclusive_status++)
						goto error;
				}

				if (sigcheck_gpg_status[i].result)
					sigc->result = sigcheck_gpg_status[i].result;
				/* Do we have key information? */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_KEYID) {
					next = strchrnul(line, ' ');
					replace_cstring(&sigc->key, line, next);
					/* Do we have signer information? */
					if (*next && (sigcheck_gpg_status[i].flags & GPG_STATUS_UID)) {
						line = next + 1;
						next = strchrnul(line, '\n');
						replace_cstring(&sigc->signer, line, next);
					}
				}

				/* Do we have trust level? */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_TRUST_LEVEL) {
					/*
					 * GPG v1 and v2 differs in how the
					 * TRUST_ lines are written.  Some
					 * trust lines contain no additional
					 * space-separated information for v1.
					 */
					size_t trust_size = strcspn(line, " \n");
					char *trust = xmemdupz(line, trust_size);

					if (parse_gpg_trust_level(trust, &sigc->trust_level)) {
						free(trust);
						goto error;
					}
					free(trust);
				}

				/* Do we have fingerprint? */
				if (sigcheck_gpg_status[i].flags & GPG_STATUS_FINGERPRINT) {
					const char *limit;
					char **field;

					next = strchrnul(line, ' ');
					replace_cstring(&sigc->fingerprint, line, next);

					/*
					 * Skip interim fields.  The search is
					 * limited to the same line since only
					 * OpenPGP signatures has a field with
					 * the primary fingerprint.
					 */
					limit = strchrnul(line, '\n');
					for (j = 9; j > 0; j--) {
						if (!*next || limit <= next)
							break;
						line = next + 1;
						next = strchrnul(line, ' ');
					}

					field = &sigc->primary_key_fingerprint;
					if (!j) {
						next = strchrnul(line, '\n');
						replace_cstring(field, line, next);
					} else {
						replace_cstring(field, NULL, NULL);
					}
				}

				break;
			}
		}
	}
	return;

error:
	sigc->result = 'E';
	/* Clear partial data to avoid confusion */
	FREE_AND_NULL(sigc->primary_key_fingerprint);
	FREE_AND_NULL(sigc->fingerprint);
	FREE_AND_NULL(sigc->signer);
	FREE_AND_NULL(sigc->key);
}

static int verify_gpg_signed_buffer(struct signature_check *sigc,
				    struct gpg_format *fmt,
				    const char *signature,
				    size_t signature_size)
{
	struct child_process gpg = CHILD_PROCESS_INIT;
	struct tempfile *temp;
	int ret;
	struct strbuf gpg_stdout = STRBUF_INIT;
	struct strbuf gpg_stderr = STRBUF_INIT;

	temp = mks_tempfile_t(".git_vtag_tmpXXXXXX");
	if (!temp)
		return error_errno(_("could not create temporary file"));
	if (write_in_full(temp->fd, signature, signature_size) < 0 ||
	    close_tempfile_gently(temp) < 0) {
		error_errno(_("failed writing detached signature to '%s'"),
			    temp->filename.buf);
		delete_tempfile(&temp);
		return -1;
	}

	strvec_push(&gpg.args, fmt->program);
	strvec_pushv(&gpg.args, fmt->verify_args);
	strvec_pushl(&gpg.args,
		     "--status-fd=1",
		     "--verify", temp->filename.buf, "-",
		     NULL);

	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&gpg, sigc->payload, sigc->payload_len, &gpg_stdout, 0,
			   &gpg_stderr, 0);
	sigchain_pop(SIGPIPE);

	delete_tempfile(&temp);

	ret |= !strstr(gpg_stdout.buf, "\n[GNUPG:] GOODSIG ");
	sigc->output = strbuf_detach(&gpg_stderr, NULL);
	sigc->gpg_status = strbuf_detach(&gpg_stdout, NULL);

	parse_gpg_output(sigc);

	strbuf_release(&gpg_stdout);
	strbuf_release(&gpg_stderr);

	return ret;
}

static void parse_ssh_output(struct signature_check *sigc)
{
	const char *line, *principal, *search;
	char *to_free;
	char *key = NULL;

	/*
	 * ssh-keygen output should be:
	 * Good "git" signature for PRINCIPAL with RSA key SHA256:FINGERPRINT
	 *
	 * or for valid but unknown keys:
	 * Good "git" signature with RSA key SHA256:FINGERPRINT
	 *
	 * Note that "PRINCIPAL" can contain whitespace, "RSA" and
	 * "SHA256" part could be a different token that names of
	 * the algorithms used, and "FINGERPRINT" is a hexadecimal
	 * string.  By finding the last occurence of " with ", we can
	 * reliably parse out the PRINCIPAL.
	 */
	sigc->result = 'B';
	sigc->trust_level = TRUST_NEVER;

	line = to_free = xmemdupz(sigc->output, strcspn(sigc->output, "\n"));

	if (skip_prefix(line, "Good \"git\" signature for ", &line)) {
		/* Search for the last "with" to get the full principal */
		principal = line;
		do {
			search = strstr(line, " with ");
			if (search)
				line = search + 1;
		} while (search != NULL);
		if (line == principal)
			goto cleanup;

		/* Valid signature and known principal */
		sigc->result = 'G';
		sigc->trust_level = TRUST_FULLY;
		sigc->signer = xmemdupz(principal, line - principal - 1);
	} else if (skip_prefix(line, "Good \"git\" signature with ", &line)) {
		/* Valid signature, but key unknown */
		sigc->result = 'G';
		sigc->trust_level = TRUST_UNDEFINED;
	} else {
		goto cleanup;
	}

	key = strstr(line, "key ");
	if (key) {
		sigc->fingerprint = xstrdup(strstr(line, "key ") + 4);
		sigc->key = xstrdup(sigc->fingerprint);
	} else {
		/*
		 * Output did not match what we expected
		 * Treat the signature as bad
		 */
		sigc->result = 'B';
	}

cleanup:
	free(to_free);
}

static int verify_ssh_signed_buffer(struct signature_check *sigc,
				    struct gpg_format *fmt,
				    const char *signature,
				    size_t signature_size)
{
	struct child_process ssh_keygen = CHILD_PROCESS_INIT;
	struct tempfile *buffer_file;
	int ret = -1;
	const char *line;
	char *principal;
	struct strbuf ssh_principals_out = STRBUF_INIT;
	struct strbuf ssh_principals_err = STRBUF_INIT;
	struct strbuf ssh_keygen_out = STRBUF_INIT;
	struct strbuf ssh_keygen_err = STRBUF_INIT;
	struct strbuf verify_time = STRBUF_INIT;
	const struct date_mode verify_date_mode = {
		.type = DATE_STRFTIME,
		.strftime_fmt = "%Y%m%d%H%M%S",
		/* SSH signing key validity has no timezone information - Use the local timezone */
		.local = 1,
	};

	if (!ssh_allowed_signers) {
		error(_("gpg.ssh.allowedSignersFile needs to be configured and exist for ssh signature verification"));
		return -1;
	}

	buffer_file = mks_tempfile_t(".git_vtag_tmpXXXXXX");
	if (!buffer_file)
		return error_errno(_("could not create temporary file"));
	if (write_in_full(buffer_file->fd, signature, signature_size) < 0 ||
	    close_tempfile_gently(buffer_file) < 0) {
		error_errno(_("failed writing detached signature to '%s'"),
			    buffer_file->filename.buf);
		delete_tempfile(&buffer_file);
		return -1;
	}

	if (sigc->payload_timestamp)
		strbuf_addf(&verify_time, "-Overify-time=%s",
			show_date(sigc->payload_timestamp, 0, &verify_date_mode));

	/* Find the principal from the signers */
	strvec_pushl(&ssh_keygen.args, fmt->program,
		     "-Y", "find-principals",
		     "-f", ssh_allowed_signers,
		     "-s", buffer_file->filename.buf,
		     verify_time.buf,
		     NULL);
	ret = pipe_command(&ssh_keygen, NULL, 0, &ssh_principals_out, 0,
			   &ssh_principals_err, 0);
	if (ret && strstr(ssh_principals_err.buf, "usage:")) {
		error(_("ssh-keygen -Y find-principals/verify is needed for ssh signature verification (available in openssh version 8.2p1+)"));
		goto out;
	}
	if (ret || !ssh_principals_out.len) {
		/*
		 * We did not find a matching principal in the allowedSigners
		 * Check without validation
		 */
		child_process_init(&ssh_keygen);
		strvec_pushl(&ssh_keygen.args, fmt->program,
			     "-Y", "check-novalidate",
			     "-n", "git",
			     "-s", buffer_file->filename.buf,
			     verify_time.buf,
			     NULL);
		pipe_command(&ssh_keygen, sigc->payload, sigc->payload_len,
				   &ssh_keygen_out, 0, &ssh_keygen_err, 0);

		/*
		 * Fail on unknown keys
		 * we still call check-novalidate to display the signature info
		 */
		ret = -1;
	} else {
		/* Check every principal we found (one per line) */
		const char *next;
		for (line = ssh_principals_out.buf;
		     *line;
		     line = next) {
			const char *end_of_text;

			next = end_of_text = strchrnul(line, '\n');

			 /* Did we find a LF, and did we have CR before it? */
			if (*end_of_text &&
			    line < end_of_text &&
			    end_of_text[-1] == '\r')
				end_of_text--;

			/* Unless we hit NUL, skip over the LF we found */
			if (*next)
				next++;

			/* Not all lines are data.  Skip empty ones */
			if (line == end_of_text)
				continue;

			/* We now know we have an non-empty line. Process it */
			principal = xmemdupz(line, end_of_text - line);

			child_process_init(&ssh_keygen);
			strbuf_release(&ssh_keygen_out);
			strbuf_release(&ssh_keygen_err);
			strvec_push(&ssh_keygen.args, fmt->program);
			/*
			 * We found principals
			 * Try with each until we find a match
			 */
			strvec_pushl(&ssh_keygen.args, "-Y", "verify",
				     "-n", "git",
				     "-f", ssh_allowed_signers,
				     "-I", principal,
				     "-s", buffer_file->filename.buf,
				     verify_time.buf,
				     NULL);

			if (ssh_revocation_file) {
				if (file_exists(ssh_revocation_file)) {
					strvec_pushl(&ssh_keygen.args, "-r",
						     ssh_revocation_file, NULL);
				} else {
					warning(_("ssh signing revocation file configured but not found: %s"),
						ssh_revocation_file);
				}
			}

			sigchain_push(SIGPIPE, SIG_IGN);
			ret = pipe_command(&ssh_keygen, sigc->payload, sigc->payload_len,
					   &ssh_keygen_out, 0, &ssh_keygen_err, 0);
			sigchain_pop(SIGPIPE);

			FREE_AND_NULL(principal);

			if (!ret)
				ret = !starts_with(ssh_keygen_out.buf, "Good");

			if (!ret)
				break;
		}
	}

	strbuf_stripspace(&ssh_keygen_out, '\0');
	strbuf_stripspace(&ssh_keygen_err, '\0');
	/* Add stderr outputs to show the user actual ssh-keygen errors */
	strbuf_add(&ssh_keygen_out, ssh_principals_err.buf, ssh_principals_err.len);
	strbuf_add(&ssh_keygen_out, ssh_keygen_err.buf, ssh_keygen_err.len);
	sigc->output = strbuf_detach(&ssh_keygen_out, NULL);
	sigc->gpg_status = xstrdup(sigc->output);

	parse_ssh_output(sigc);

out:
	if (buffer_file)
		delete_tempfile(&buffer_file);
	strbuf_release(&ssh_principals_out);
	strbuf_release(&ssh_principals_err);
	strbuf_release(&ssh_keygen_out);
	strbuf_release(&ssh_keygen_err);
	strbuf_release(&verify_time);

	return ret;
}

static int parse_payload_metadata(struct signature_check *sigc)
{
	const char *ident_line = NULL;
	size_t ident_len;
	struct ident_split ident;
	const char *signer_header;

	switch (sigc->payload_type) {
	case SIGNATURE_PAYLOAD_COMMIT:
		signer_header = "committer";
		break;
	case SIGNATURE_PAYLOAD_TAG:
		signer_header = "tagger";
		break;
	case SIGNATURE_PAYLOAD_UNDEFINED:
	case SIGNATURE_PAYLOAD_PUSH_CERT:
		/* Ignore payloads we don't want to parse */
		return 0;
	default:
		BUG("invalid value for sigc->payload_type");
	}

	ident_line = find_commit_header(sigc->payload, signer_header, &ident_len);
	if (!ident_line || !ident_len)
		return 1;

	if (split_ident_line(&ident, ident_line, ident_len))
		return 1;

	if (!sigc->payload_timestamp && ident.date_begin && ident.date_end)
		sigc->payload_timestamp = parse_timestamp(ident.date_begin, NULL, 10);

	return 0;
}

int check_signature(struct signature_check *sigc,
		    const char *signature, size_t slen)
{
	struct gpg_format *fmt;
	int status;

	gpg_interface_lazy_init();

	sigc->result = 'N';
	sigc->trust_level = TRUST_UNDEFINED;

	fmt = get_format_by_sig(signature);
	if (!fmt)
		die(_("bad/incompatible signature '%s'"), signature);

	if (parse_payload_metadata(sigc))
		return 1;

	status = fmt->verify_signed_buffer(sigc, fmt, signature, slen);

	if (status && !sigc->output)
		return !!status;

	status |= sigc->result != 'G';
	status |= sigc->trust_level < configured_min_trust_level;

	return !!status;
}

void print_signature_buffer(const struct signature_check *sigc, unsigned flags)
{
	const char *output = flags & GPG_VERIFY_RAW ? sigc->gpg_status :
							    sigc->output;

	if (flags & GPG_VERIFY_VERBOSE && sigc->payload)
		fwrite(sigc->payload, 1, sigc->payload_len, stdout);

	if (output)
		fputs(output, stderr);
}

size_t parse_signed_buffer(const char *buf, size_t size)
{
	size_t len = 0;
	size_t match = size;
	while (len < size) {
		const char *eol;

		if (get_format_by_sig(buf + len))
			match = len;

		eol = memchr(buf + len, '\n', size - len);
		len += eol ? eol - (buf + len) + 1 : size - len;
	}
	return match;
}

int parse_signature(const char *buf, size_t size, struct strbuf *payload, struct strbuf *signature)
{
	size_t match = parse_signed_buffer(buf, size);
	if (match != size) {
		strbuf_add(payload, buf, match);
		remove_signature(payload);
		strbuf_add(signature, buf + match, size - match);
		return 1;
	}
	return 0;
}

void set_signing_key(const char *key)
{
	gpg_interface_lazy_init();

	free(configured_signing_key);
	configured_signing_key = xstrdup(key);
}

static int git_gpg_config(const char *var, const char *value,
			  const struct config_context *ctx UNUSED,
			  void *cb UNUSED)
{
	struct gpg_format *fmt = NULL;
	char *fmtname = NULL;
	char *trust;
	int ret;

	if (!strcmp(var, "user.signingkey")) {
		if (!value)
			return config_error_nonbool(var);
		set_signing_key(value);
		return 0;
	}

	if (!strcmp(var, "gpg.format")) {
		if (!value)
			return config_error_nonbool(var);
		fmt = get_format_by_name(value);
		if (!fmt)
			return error(_("invalid value for '%s': '%s'"),
				     var, value);
		use_format = fmt;
		return 0;
	}

	if (!strcmp(var, "gpg.mintrustlevel")) {
		if (!value)
			return config_error_nonbool(var);

		trust = xstrdup_toupper(value);
		ret = parse_gpg_trust_level(trust, &configured_min_trust_level);
		free(trust);

		if (ret)
			return error(_("invalid value for '%s': '%s'"),
				     var, value);
		return 0;
	}

	if (!strcmp(var, "gpg.ssh.defaultkeycommand")) {
		if (!value)
			return config_error_nonbool(var);
		return git_config_string(&ssh_default_key_command, var, value);
	}

	if (!strcmp(var, "gpg.ssh.allowedsignersfile")) {
		if (!value)
			return config_error_nonbool(var);
		return git_config_pathname(&ssh_allowed_signers, var, value);
	}

	if (!strcmp(var, "gpg.ssh.revocationfile")) {
		if (!value)
			return config_error_nonbool(var);
		return git_config_pathname(&ssh_revocation_file, var, value);
	}

	if (!strcmp(var, "gpg.program") || !strcmp(var, "gpg.openpgp.program"))
		fmtname = "openpgp";

	if (!strcmp(var, "gpg.x509.program"))
		fmtname = "x509";

	if (!strcmp(var, "gpg.ssh.program"))
		fmtname = "ssh";

	if (fmtname) {
		fmt = get_format_by_name(fmtname);
		return git_config_string(&fmt->program, var, value);
	}

	return 0;
}

/*
 * Returns 1 if `string` contains a literal ssh key, 0 otherwise
 * `key` will be set to the start of the actual key if a prefix is present.
 */
static int is_literal_ssh_key(const char *string, const char **key)
{
	if (skip_prefix(string, "key::", key))
		return 1;
	if (starts_with(string, "ssh-")) {
		*key = string;
		return 1;
	}
	return 0;
}

static char *get_ssh_key_fingerprint(const char *signing_key)
{
	struct child_process ssh_keygen = CHILD_PROCESS_INIT;
	int ret = -1;
	struct strbuf fingerprint_stdout = STRBUF_INIT;
	struct strbuf **fingerprint;
	char *fingerprint_ret;
	const char *literal_key = NULL;

	/*
	 * With SSH Signing this can contain a filename or a public key
	 * For textual representation we usually want a fingerprint
	 */
	if (is_literal_ssh_key(signing_key, &literal_key)) {
		strvec_pushl(&ssh_keygen.args, "ssh-keygen", "-lf", "-", NULL);
		ret = pipe_command(&ssh_keygen, literal_key,
				   strlen(literal_key), &fingerprint_stdout, 0,
				   NULL, 0);
	} else {
		strvec_pushl(&ssh_keygen.args, "ssh-keygen", "-lf",
			     configured_signing_key, NULL);
		ret = pipe_command(&ssh_keygen, NULL, 0, &fingerprint_stdout, 0,
				   NULL, 0);
	}

	if (!!ret)
		die_errno(_("failed to get the ssh fingerprint for key '%s'"),
			  signing_key);

	fingerprint = strbuf_split_max(&fingerprint_stdout, ' ', 3);
	if (!fingerprint[1])
		die_errno(_("failed to get the ssh fingerprint for key '%s'"),
			  signing_key);

	fingerprint_ret = strbuf_detach(fingerprint[1], NULL);
	strbuf_list_free(fingerprint);
	strbuf_release(&fingerprint_stdout);
	return fingerprint_ret;
}

/* Returns the first public key from an ssh-agent to use for signing */
static const char *get_default_ssh_signing_key(void)
{
	struct child_process ssh_default_key = CHILD_PROCESS_INIT;
	int ret = -1;
	struct strbuf key_stdout = STRBUF_INIT, key_stderr = STRBUF_INIT;
	struct strbuf **keys;
	char *key_command = NULL;
	const char **argv;
	int n;
	char *default_key = NULL;
	const char *literal_key = NULL;

	if (!ssh_default_key_command)
		die(_("either user.signingkey or gpg.ssh.defaultKeyCommand needs to be configured"));

	key_command = xstrdup(ssh_default_key_command);
	n = split_cmdline(key_command, &argv);

	if (n < 0)
		die("malformed build-time gpg.ssh.defaultKeyCommand: %s",
		    split_cmdline_strerror(n));

	strvec_pushv(&ssh_default_key.args, argv);
	ret = pipe_command(&ssh_default_key, NULL, 0, &key_stdout, 0,
			   &key_stderr, 0);

	if (!ret) {
		keys = strbuf_split_max(&key_stdout, '\n', 2);
		if (keys[0] && is_literal_ssh_key(keys[0]->buf, &literal_key)) {
			/*
			 * We only use `is_literal_ssh_key` here to check validity
			 * The prefix will be stripped when the key is used.
			 */
			default_key = strbuf_detach(keys[0], NULL);
		} else {
			warning(_("gpg.ssh.defaultKeyCommand succeeded but returned no keys: %s %s"),
				key_stderr.buf, key_stdout.buf);
		}

		strbuf_list_free(keys);
	} else {
		warning(_("gpg.ssh.defaultKeyCommand failed: %s %s"),
			key_stderr.buf, key_stdout.buf);
	}

	free(key_command);
	free(argv);
	strbuf_release(&key_stdout);

	return default_key;
}

static const char *get_ssh_key_id(void) {
	return get_ssh_key_fingerprint(get_signing_key());
}

/* Returns a textual but unique representation of the signing key */
const char *get_signing_key_id(void)
{
	gpg_interface_lazy_init();

	if (use_format->get_key_id) {
		return use_format->get_key_id();
	}

	/* GPG/GPGSM only store a key id on this variable */
	return get_signing_key();
}

const char *get_signing_key(void)
{
	gpg_interface_lazy_init();

	if (configured_signing_key)
		return configured_signing_key;
	if (use_format->get_default_key) {
		return use_format->get_default_key();
	}

	return git_committer_info(IDENT_STRICT | IDENT_NO_DATE);
}

const char *gpg_trust_level_to_str(enum signature_trust_level level)
{
	struct sigcheck_gpg_trust_level *trust;

	if (level < 0 || level >= ARRAY_SIZE(sigcheck_gpg_trust_level))
		BUG("invalid trust level requested %d", level);

	trust = &sigcheck_gpg_trust_level[level];
	if (trust->value != level)
		BUG("sigcheck_gpg_trust_level[] unsorted");

	return sigcheck_gpg_trust_level[level].display_key;
}

int sign_buffer(struct strbuf *buffer, struct strbuf *signature, const char *signing_key)
{
	gpg_interface_lazy_init();

	return use_format->sign_buffer(buffer, signature, signing_key);
}

/*
 * Strip CR from the line endings, in case we are on Windows.
 * NEEDSWORK: make it trim only CRs before LFs and rename
 */
static void remove_cr_after(struct strbuf *buffer, size_t offset)
{
	size_t i, j;

	for (i = j = offset; i < buffer->len; i++) {
		if (buffer->buf[i] != '\r') {
			if (i != j)
				buffer->buf[j] = buffer->buf[i];
			j++;
		}
	}
	strbuf_setlen(buffer, j);
}

static int sign_buffer_gpg(struct strbuf *buffer, struct strbuf *signature,
			  const char *signing_key)
{
	struct child_process gpg = CHILD_PROCESS_INIT;
	int ret;
	size_t bottom;
	const char *cp;
	struct strbuf gpg_status = STRBUF_INIT;

	strvec_pushl(&gpg.args,
		     use_format->program,
		     "--status-fd=2",
		     "-bsau", signing_key,
		     NULL);

	bottom = signature->len;

	/*
	 * When the username signingkey is bad, program could be terminated
	 * because gpg exits without reading and then write gets SIGPIPE.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&gpg, buffer->buf, buffer->len,
			   signature, 1024, &gpg_status, 0);
	sigchain_pop(SIGPIPE);

	for (cp = gpg_status.buf;
	     cp && (cp = strstr(cp, "[GNUPG:] SIG_CREATED "));
	     cp++) {
		if (cp == gpg_status.buf || cp[-1] == '\n')
			break; /* found */
	}
	ret |= !cp;
	if (ret) {
		error(_("gpg failed to sign the data:\n%s"),
		      gpg_status.len ? gpg_status.buf : "(no gpg output)");
		strbuf_release(&gpg_status);
		return -1;
	}
	strbuf_release(&gpg_status);

	/* Strip CR from the line endings, in case we are on Windows. */
	remove_cr_after(signature, bottom);

	return 0;
}

static int sign_buffer_ssh(struct strbuf *buffer, struct strbuf *signature,
			   const char *signing_key)
{
	struct child_process signer = CHILD_PROCESS_INIT;
	int ret = -1;
	size_t bottom, keylen;
	struct strbuf signer_stderr = STRBUF_INIT;
	struct tempfile *key_file = NULL, *buffer_file = NULL;
	char *ssh_signing_key_file = NULL;
	struct strbuf ssh_signature_filename = STRBUF_INIT;
	const char *literal_key = NULL;
	int literal_ssh_key = 0;

	if (!signing_key || signing_key[0] == '\0')
		return error(
			_("user.signingKey needs to be set for ssh signing"));

	if (is_literal_ssh_key(signing_key, &literal_key)) {
		/* A literal ssh key */
		literal_ssh_key = 1;
		key_file = mks_tempfile_t(".git_signing_key_tmpXXXXXX");
		if (!key_file)
			return error_errno(
				_("could not create temporary file"));
		keylen = strlen(literal_key);
		if (write_in_full(key_file->fd, literal_key, keylen) < 0 ||
		    close_tempfile_gently(key_file) < 0) {
			error_errno(_("failed writing ssh signing key to '%s'"),
				    key_file->filename.buf);
			goto out;
		}
		ssh_signing_key_file = strbuf_detach(&key_file->filename, NULL);
	} else {
		/* We assume a file */
		ssh_signing_key_file = interpolate_path(signing_key, 1);
	}

	buffer_file = mks_tempfile_t(".git_signing_buffer_tmpXXXXXX");
	if (!buffer_file) {
		error_errno(_("could not create temporary file"));
		goto out;
	}

	if (write_in_full(buffer_file->fd, buffer->buf, buffer->len) < 0 ||
	    close_tempfile_gently(buffer_file) < 0) {
		error_errno(_("failed writing ssh signing key buffer to '%s'"),
			    buffer_file->filename.buf);
		goto out;
	}

	strvec_pushl(&signer.args, use_format->program,
		     "-Y", "sign",
		     "-n", "git",
		     "-f", ssh_signing_key_file,
		     NULL);
	if (literal_ssh_key)
		strvec_push(&signer.args, "-U");
	strvec_push(&signer.args, buffer_file->filename.buf);

	sigchain_push(SIGPIPE, SIG_IGN);
	ret = pipe_command(&signer, NULL, 0, NULL, 0, &signer_stderr, 0);
	sigchain_pop(SIGPIPE);

	if (ret) {
		if (strstr(signer_stderr.buf, "usage:"))
			error(_("ssh-keygen -Y sign is needed for ssh signing (available in openssh version 8.2p1+)"));

		error("%s", signer_stderr.buf);
		goto out;
	}

	bottom = signature->len;

	strbuf_addbuf(&ssh_signature_filename, &buffer_file->filename);
	strbuf_addstr(&ssh_signature_filename, ".sig");
	if (strbuf_read_file(signature, ssh_signature_filename.buf, 0) < 0) {
		ret = error_errno(
			_("failed reading ssh signing data buffer from '%s'"),
			ssh_signature_filename.buf);
		goto out;
	}
	/* Strip CR from the line endings, in case we are on Windows. */
	remove_cr_after(signature, bottom);

out:
	if (key_file)
		delete_tempfile(&key_file);
	if (buffer_file)
		delete_tempfile(&buffer_file);
	if (ssh_signature_filename.len)
		unlink_or_warn(ssh_signature_filename.buf);
	strbuf_release(&signer_stderr);
	strbuf_release(&ssh_signature_filename);
	FREE_AND_NULL(ssh_signing_key_file);
	return ret;
}
