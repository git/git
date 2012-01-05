#include "cache.h"
#include "run-command.h"
#include "strbuf.h"
#include "gpg-interface.h"
#include "sigchain.h"

static char *configured_signing_key;
static const char *gpg_program = "gpg";

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
	return git_committer_info(IDENT_ERROR_ON_NO_NAME|IDENT_NO_DATE);
}

/*
 * Create a detached signature for the contents of "buffer" and append
 * it after "signature"; "buffer" and "signature" can be the same
 * strbuf instance, which would cause the detached signature appended
 * at the end.
 */
int sign_buffer(struct strbuf *buffer, struct strbuf *signature, const char *signing_key)
{
	struct child_process gpg;
	const char *args[4];
	ssize_t len;
	size_t i, j, bottom;

	memset(&gpg, 0, sizeof(gpg));
	gpg.argv = args;
	gpg.in = -1;
	gpg.out = -1;
	args[0] = gpg_program;
	args[1] = "-bsau";
	args[2] = signing_key;
	args[3] = NULL;

	if (start_command(&gpg))
		return error(_("could not run gpg."));

	/*
	 * When the username signingkey is bad, program could be terminated
	 * because gpg exits without reading and then write gets SIGPIPE.
	 */
	sigchain_push(SIGPIPE, SIG_IGN);

	if (write_in_full(gpg.in, buffer->buf, buffer->len) != buffer->len) {
		close(gpg.in);
		close(gpg.out);
		finish_command(&gpg);
		return error(_("gpg did not accept the data"));
	}
	close(gpg.in);

	bottom = signature->len;
	len = strbuf_read(signature, gpg.out, 1024);
	close(gpg.out);

	sigchain_pop(SIGPIPE);

	if (finish_command(&gpg) || !len || len < 0)
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
 */
int verify_signed_buffer(const char *payload, size_t payload_size,
			 const char *signature, size_t signature_size,
			 struct strbuf *gpg_output)
{
	struct child_process gpg;
	const char *args_gpg[] = {NULL, "--verify", "FILE", "-", NULL};
	char path[PATH_MAX];
	int fd, ret;

	args_gpg[0] = gpg_program;
	fd = git_mkstemp(path, PATH_MAX, ".git_vtag_tmpXXXXXX");
	if (fd < 0)
		return error("could not create temporary file '%s': %s",
			     path, strerror(errno));
	if (write_in_full(fd, signature, signature_size) < 0)
		return error("failed writing detached signature to '%s': %s",
			     path, strerror(errno));
	close(fd);

	memset(&gpg, 0, sizeof(gpg));
	gpg.argv = args_gpg;
	gpg.in = -1;
	if (gpg_output)
		gpg.err = -1;
	args_gpg[2] = path;
	if (start_command(&gpg)) {
		unlink(path);
		return error("could not run gpg.");
	}

	write_in_full(gpg.in, payload, payload_size);
	close(gpg.in);

	if (gpg_output)
		strbuf_read(gpg_output, gpg.err, 0);
	ret = finish_command(&gpg);

	unlink_or_warn(path);

	return ret;
}
