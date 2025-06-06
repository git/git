#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "builtin.h"
#include "abspath.h"

#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "lockfile.h"
#include "pack.h"
#include "refs.h"
#include "pkt-line.h"
#include "sideband.h"
#include "run-command.h"
#include "hook.h"
#include "exec-cmd.h"
#include "commit.h"
#include "object.h"
#include "remote.h"
#include "connect.h"
#include "string-list.h"
#include "oid-array.h"
#include "connected.h"
#include "strvec.h"
#include "version.h"
#include "gpg-interface.h"
#include "sigchain.h"
#include "fsck.h"
#include "tmp-objdir.h"
#include "oidset.h"
#include "packfile.h"
#include "object-file.h"
#include "object-name.h"
#include "odb.h"
#include "path.h"
#include "protocol.h"
#include "commit-reach.h"
#include "server-info.h"
#include "trace.h"
#include "trace2.h"
#include "worktree.h"
#include "shallow.h"
#include "parse-options.h"

static const char * const receive_pack_usage[] = {
	N_("git receive-pack <git-dir>"),
	NULL
};

enum deny_action {
	DENY_UNCONFIGURED,
	DENY_IGNORE,
	DENY_WARN,
	DENY_REFUSE,
	DENY_UPDATE_INSTEAD
};

static int deny_deletes;
static int deny_non_fast_forwards;
static enum deny_action deny_current_branch = DENY_UNCONFIGURED;
static enum deny_action deny_delete_current = DENY_UNCONFIGURED;
static int receive_fsck_objects = -1;
static int transfer_fsck_objects = -1;
static struct strbuf fsck_msg_types = STRBUF_INIT;
static int receive_unpack_limit = -1;
static int transfer_unpack_limit = -1;
static int advertise_atomic_push = 1;
static int advertise_push_options;
static int advertise_sid;
static int unpack_limit = 100;
static off_t max_input_size;
static int report_status;
static int report_status_v2;
static int use_sideband;
static int use_atomic;
static int use_push_options;
static int quiet;
static int prefer_ofs_delta = 1;
static int auto_update_server_info;
static int auto_gc = 1;
static int reject_thin;
static int skip_connectivity_check;
static int stateless_rpc;
static const char *service_dir;
static const char *head_name;
static void *head_name_to_free;
static int sent_capabilities;
static int shallow_update;
static const char *alt_shallow_file;
static struct strbuf push_cert = STRBUF_INIT;
static struct object_id push_cert_oid;
static struct signature_check sigcheck;
static const char *push_cert_nonce;
static char *cert_nonce_seed;
static struct strvec hidden_refs = STRVEC_INIT;

static const char *NONCE_UNSOLICITED = "UNSOLICITED";
static const char *NONCE_BAD = "BAD";
static const char *NONCE_MISSING = "MISSING";
static const char *NONCE_OK = "OK";
static const char *NONCE_SLOP = "SLOP";
static const char *nonce_status;
static long nonce_stamp_slop;
static timestamp_t nonce_stamp_slop_limit;
static struct ref_transaction *transaction;

static enum {
	KEEPALIVE_NEVER = 0,
	KEEPALIVE_AFTER_NUL,
	KEEPALIVE_ALWAYS
} use_keepalive;
static int keepalive_in_sec = 5;

static struct tmp_objdir *tmp_objdir;

static struct proc_receive_ref {
	unsigned int want_add:1,
		     want_delete:1,
		     want_modify:1,
		     negative_ref:1;
	char *ref_prefix;
	struct proc_receive_ref *next;
} *proc_receive_ref;

static void proc_receive_ref_append(const char *prefix);

static enum deny_action parse_deny_action(const char *var, const char *value)
{
	if (value) {
		if (!strcasecmp(value, "ignore"))
			return DENY_IGNORE;
		if (!strcasecmp(value, "warn"))
			return DENY_WARN;
		if (!strcasecmp(value, "refuse"))
			return DENY_REFUSE;
		if (!strcasecmp(value, "updateinstead"))
			return DENY_UPDATE_INSTEAD;
	}
	if (git_config_bool(var, value))
		return DENY_REFUSE;
	return DENY_IGNORE;
}

static int receive_pack_config(const char *var, const char *value,
			       const struct config_context *ctx, void *cb)
{
	const char *msg_id;
	int status = parse_hide_refs_config(var, value, "receive", &hidden_refs);

	if (status)
		return status;

	if (strcmp(var, "receive.denydeletes") == 0) {
		deny_deletes = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.denynonfastforwards") == 0) {
		deny_non_fast_forwards = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.unpacklimit") == 0) {
		receive_unpack_limit = git_config_int(var, value, ctx->kvi);
		return 0;
	}

	if (strcmp(var, "transfer.unpacklimit") == 0) {
		transfer_unpack_limit = git_config_int(var, value, ctx->kvi);
		return 0;
	}

	if (strcmp(var, "receive.fsck.skiplist") == 0) {
		char *path;

		if (git_config_pathname(&path, var, value))
			return -1;
		strbuf_addf(&fsck_msg_types, "%cskiplist=%s",
			fsck_msg_types.len ? ',' : '=', path);
		free(path);
		return 0;
	}

	if (skip_prefix(var, "receive.fsck.", &msg_id)) {
		if (!value)
			return config_error_nonbool(var);
		if (is_valid_msg_type(msg_id, value))
			strbuf_addf(&fsck_msg_types, "%c%s=%s",
				fsck_msg_types.len ? ',' : '=', msg_id, value);
		else
			warning("skipping unknown msg id '%s'", msg_id);
		return 0;
	}

	if (strcmp(var, "receive.fsckobjects") == 0) {
		receive_fsck_objects = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "transfer.fsckobjects") == 0) {
		transfer_fsck_objects = git_config_bool(var, value);
		return 0;
	}

	if (!strcmp(var, "receive.denycurrentbranch")) {
		deny_current_branch = parse_deny_action(var, value);
		return 0;
	}

	if (strcmp(var, "receive.denydeletecurrent") == 0) {
		deny_delete_current = parse_deny_action(var, value);
		return 0;
	}

	if (strcmp(var, "repack.usedeltabaseoffset") == 0) {
		prefer_ofs_delta = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.updateserverinfo") == 0) {
		auto_update_server_info = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.autogc") == 0) {
		auto_gc = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.shallowupdate") == 0) {
		shallow_update = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.certnonceseed") == 0)
		return git_config_string(&cert_nonce_seed, var, value);

	if (strcmp(var, "receive.certnonceslop") == 0) {
		nonce_stamp_slop_limit = git_config_ulong(var, value, ctx->kvi);
		return 0;
	}

	if (strcmp(var, "receive.advertiseatomic") == 0) {
		advertise_atomic_push = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.advertisepushoptions") == 0) {
		advertise_push_options = git_config_bool(var, value);
		return 0;
	}

	if (strcmp(var, "receive.keepalive") == 0) {
		keepalive_in_sec = git_config_int(var, value, ctx->kvi);
		return 0;
	}

	if (strcmp(var, "receive.maxinputsize") == 0) {
		max_input_size = git_config_int64(var, value, ctx->kvi);
		return 0;
	}

	if (strcmp(var, "receive.procreceiverefs") == 0) {
		if (!value)
			return config_error_nonbool(var);
		proc_receive_ref_append(value);
		return 0;
	}

	if (strcmp(var, "transfer.advertisesid") == 0) {
		advertise_sid = git_config_bool(var, value);
		return 0;
	}

	return git_default_config(var, value, ctx, cb);
}

static void show_ref(const char *path, const struct object_id *oid)
{
	if (sent_capabilities) {
		packet_write_fmt(1, "%s %s\n", oid_to_hex(oid), path);
	} else {
		struct strbuf cap = STRBUF_INIT;

		strbuf_addstr(&cap,
			      "report-status report-status-v2 delete-refs side-band-64k quiet");
		if (advertise_atomic_push)
			strbuf_addstr(&cap, " atomic");
		if (prefer_ofs_delta)
			strbuf_addstr(&cap, " ofs-delta");
		if (push_cert_nonce)
			strbuf_addf(&cap, " push-cert=%s", push_cert_nonce);
		if (advertise_push_options)
			strbuf_addstr(&cap, " push-options");
		if (advertise_sid)
			strbuf_addf(&cap, " session-id=%s", trace2_session_id());
		strbuf_addf(&cap, " object-format=%s", the_hash_algo->name);
		strbuf_addf(&cap, " agent=%s", git_user_agent_sanitized());
		packet_write_fmt(1, "%s %s%c%s\n",
			     oid_to_hex(oid), path, 0, cap.buf);
		strbuf_release(&cap);
		sent_capabilities = 1;
	}
}

static int show_ref_cb(const char *path_full, const char *referent UNUSED, const struct object_id *oid,
		       int flag UNUSED, void *data)
{
	struct oidset *seen = data;
	const char *path = strip_namespace(path_full);

	if (ref_is_hidden(path, path_full, &hidden_refs))
		return 0;

	/*
	 * Advertise refs outside our current namespace as ".have"
	 * refs, so that the client can use them to minimize data
	 * transfer but will otherwise ignore them.
	 */
	if (!path) {
		if (oidset_insert(seen, oid))
			return 0;
		path = ".have";
	} else {
		oidset_insert(seen, oid);
	}
	show_ref(path, oid);
	return 0;
}

static void show_one_alternate_ref(const struct object_id *oid,
				   void *data)
{
	struct oidset *seen = data;

	if (oidset_insert(seen, oid))
		return;

	show_ref(".have", oid);
}

static void write_head_info(void)
{
	static struct oidset seen = OIDSET_INIT;
	struct strvec excludes_vector = STRVEC_INIT;
	const char **exclude_patterns;

	/*
	 * We need access to the reference names both with and without their
	 * namespace and thus cannot use `refs_for_each_namespaced_ref()`. We
	 * thus have to adapt exclude patterns to carry the namespace prefix
	 * ourselves.
	 */
	exclude_patterns = get_namespaced_exclude_patterns(
		hidden_refs_to_excludes(&hidden_refs),
		get_git_namespace(), &excludes_vector);

	refs_for_each_fullref_in(get_main_ref_store(the_repository), "",
				 exclude_patterns, show_ref_cb, &seen);
	odb_for_each_alternate_ref(the_repository->objects,
				   show_one_alternate_ref, &seen);

	oidset_clear(&seen);
	strvec_clear(&excludes_vector);

	if (!sent_capabilities)
		show_ref("capabilities^{}", null_oid(the_hash_algo));

	advertise_shallow_grafts(1);

	/* EOF */
	packet_flush(1);
}

#define RUN_PROC_RECEIVE_SCHEDULED	1
#define RUN_PROC_RECEIVE_RETURNED	2
struct command {
	struct command *next;
	const char *error_string;
	char *error_string_owned;
	struct ref_push_report *report;
	unsigned int skip_update:1,
		     did_not_exist:1,
		     run_proc_receive:2;
	int index;
	struct object_id old_oid;
	struct object_id new_oid;
	char ref_name[FLEX_ARRAY]; /* more */
};

static void proc_receive_ref_append(const char *prefix)
{
	struct proc_receive_ref *ref_pattern;
	char *p;
	int len;

	CALLOC_ARRAY(ref_pattern, 1);
	p = strchr(prefix, ':');
	if (p) {
		while (prefix < p) {
			if (*prefix == 'a')
				ref_pattern->want_add = 1;
			else if (*prefix == 'd')
				ref_pattern->want_delete = 1;
			else if (*prefix == 'm')
				ref_pattern->want_modify = 1;
			else if (*prefix == '!')
				ref_pattern->negative_ref = 1;
			prefix++;
		}
		prefix++;
	} else {
		ref_pattern->want_add = 1;
		ref_pattern->want_delete = 1;
		ref_pattern->want_modify = 1;
	}
	len = strlen(prefix);
	while (len && prefix[len - 1] == '/')
		len--;
	ref_pattern->ref_prefix = xmemdupz(prefix, len);
	if (!proc_receive_ref) {
		proc_receive_ref = ref_pattern;
	} else {
		struct proc_receive_ref *end;

		end = proc_receive_ref;
		while (end->next)
			end = end->next;
		end->next = ref_pattern;
	}
}

static int proc_receive_ref_matches(struct command *cmd)
{
	struct proc_receive_ref *p;

	if (!proc_receive_ref)
		return 0;

	for (p = proc_receive_ref; p; p = p->next) {
		const char *match = p->ref_prefix;
		const char *remains;

		if (!p->want_add && is_null_oid(&cmd->old_oid))
			continue;
		else if (!p->want_delete && is_null_oid(&cmd->new_oid))
			continue;
		else if (!p->want_modify &&
			 !is_null_oid(&cmd->old_oid) &&
			 !is_null_oid(&cmd->new_oid))
			continue;

		if (skip_prefix(cmd->ref_name, match, &remains) &&
		    (!*remains || *remains == '/')) {
			if (!p->negative_ref)
				return 1;
		} else if (p->negative_ref) {
			return 1;
		}
	}
	return 0;
}

static void report_message(const char *prefix, const char *err, va_list params)
{
	int sz;
	char msg[4096];

	sz = xsnprintf(msg, sizeof(msg), "%s", prefix);
	sz += vsnprintf(msg + sz, sizeof(msg) - sz, err, params);
	if (sz > (sizeof(msg) - 1))
		sz = sizeof(msg) - 1;
	msg[sz++] = '\n';

	if (use_sideband)
		send_sideband(1, 2, msg, sz, use_sideband);
	else
		xwrite(2, msg, sz);
}

__attribute__((format (printf, 1, 2)))
static void rp_warning(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	report_message("warning: ", err, params);
	va_end(params);
}

__attribute__((format (printf, 1, 2)))
static void rp_error(const char *err, ...)
{
	va_list params;
	va_start(params, err);
	report_message("error: ", err, params);
	va_end(params);
}

static int copy_to_sideband(int in, int out UNUSED, void *arg UNUSED)
{
	char data[128];
	int keepalive_active = 0;

	if (keepalive_in_sec <= 0)
		use_keepalive = KEEPALIVE_NEVER;
	if (use_keepalive == KEEPALIVE_ALWAYS)
		keepalive_active = 1;

	while (1) {
		ssize_t sz;

		if (keepalive_active) {
			struct pollfd pfd;
			int ret;

			pfd.fd = in;
			pfd.events = POLLIN;
			ret = poll(&pfd, 1, 1000 * keepalive_in_sec);

			if (ret < 0) {
				if (errno == EINTR)
					continue;
				else
					break;
			} else if (ret == 0) {
				/* no data; send a keepalive packet */
				static const char buf[] = "0005\1";
				write_or_die(1, buf, sizeof(buf) - 1);
				continue;
			} /* else there is actual data to read */
		}

		sz = xread(in, data, sizeof(data));
		if (sz <= 0)
			break;

		if (use_keepalive == KEEPALIVE_AFTER_NUL && !keepalive_active) {
			const char *p = memchr(data, '\0', sz);
			if (p) {
				/*
				 * The NUL tells us to start sending keepalives. Make
				 * sure we send any other data we read along
				 * with it.
				 */
				keepalive_active = 1;
				send_sideband(1, 2, data, p - data, use_sideband);
				send_sideband(1, 2, p + 1, sz - (p - data + 1), use_sideband);
				continue;
			}
		}

		/*
		 * Either we're not looking for a NUL signal, or we didn't see
		 * it yet; just pass along the data.
		 */
		send_sideband(1, 2, data, sz, use_sideband);
	}
	close(in);
	return 0;
}

static void hmac_hash(unsigned char *out,
		      const char *key_in, size_t key_len,
		      const char *text, size_t text_len)
{
	unsigned char key[GIT_MAX_BLKSZ];
	unsigned char k_ipad[GIT_MAX_BLKSZ];
	unsigned char k_opad[GIT_MAX_BLKSZ];
	int i;
	struct git_hash_ctx ctx;

	/* RFC 2104 2. (1) */
	memset(key, '\0', GIT_MAX_BLKSZ);
	if (the_hash_algo->blksz < key_len) {
		the_hash_algo->init_fn(&ctx);
		git_hash_update(&ctx, key_in, key_len);
		git_hash_final(key, &ctx);
	} else {
		memcpy(key, key_in, key_len);
	}

	/* RFC 2104 2. (2) & (5) */
	for (i = 0; i < sizeof(key); i++) {
		k_ipad[i] = key[i] ^ 0x36;
		k_opad[i] = key[i] ^ 0x5c;
	}

	/* RFC 2104 2. (3) & (4) */
	the_hash_algo->init_fn(&ctx);
	git_hash_update(&ctx, k_ipad, sizeof(k_ipad));
	git_hash_update(&ctx, text, text_len);
	git_hash_final(out, &ctx);

	/* RFC 2104 2. (6) & (7) */
	the_hash_algo->init_fn(&ctx);
	git_hash_update(&ctx, k_opad, sizeof(k_opad));
	git_hash_update(&ctx, out, the_hash_algo->rawsz);
	git_hash_final(out, &ctx);
}

static char *prepare_push_cert_nonce(const char *path, timestamp_t stamp)
{
	struct strbuf buf = STRBUF_INIT;
	unsigned char hash[GIT_MAX_RAWSZ];

	strbuf_addf(&buf, "%s:%"PRItime, path, stamp);
	hmac_hash(hash, buf.buf, buf.len, cert_nonce_seed, strlen(cert_nonce_seed));
	strbuf_release(&buf);

	/* RFC 2104 5. HMAC-SHA1 or HMAC-SHA256 */
	strbuf_addf(&buf, "%"PRItime"-%.*s", stamp, (int)the_hash_algo->hexsz, hash_to_hex(hash));
	return strbuf_detach(&buf, NULL);
}

/*
 * Return zero if a and b are equal up to n bytes and nonzero if they are not.
 * This operation is guaranteed to run in constant time to avoid leaking data.
 */
static int constant_memequal(const char *a, const char *b, size_t n)
{
	int res = 0;
	size_t i;

	for (i = 0; i < n; i++)
		res |= a[i] ^ b[i];
	return res;
}

static const char *check_nonce(const char *buf)
{
	size_t noncelen;
	const char *found = find_commit_header(buf, "nonce", &noncelen);
	char *nonce = found ? xmemdupz(found, noncelen) : NULL;
	timestamp_t stamp, ostamp;
	char *bohmac, *expect = NULL;
	const char *retval = NONCE_BAD;

	if (!nonce) {
		retval = NONCE_MISSING;
		goto leave;
	} else if (!push_cert_nonce) {
		retval = NONCE_UNSOLICITED;
		goto leave;
	} else if (!strcmp(push_cert_nonce, nonce)) {
		retval = NONCE_OK;
		goto leave;
	}

	if (!stateless_rpc) {
		/* returned nonce MUST match what we gave out earlier */
		retval = NONCE_BAD;
		goto leave;
	}

	/*
	 * In stateless mode, we may be receiving a nonce issued by
	 * another instance of the server that serving the same
	 * repository, and the timestamps may not match, but the
	 * nonce-seed and dir should match, so we can recompute and
	 * report the time slop.
	 *
	 * In addition, when a nonce issued by another instance has
	 * timestamp within receive.certnonceslop seconds, we pretend
	 * as if we issued that nonce when reporting to the hook.
	 */

	/* nonce is concat(<seconds-since-epoch>, "-", <hmac>) */
	if (*nonce <= '0' || '9' < *nonce) {
		retval = NONCE_BAD;
		goto leave;
	}
	stamp = parse_timestamp(nonce, &bohmac, 10);
	if (bohmac == nonce || bohmac[0] != '-') {
		retval = NONCE_BAD;
		goto leave;
	}

	expect = prepare_push_cert_nonce(service_dir, stamp);
	if (noncelen != strlen(expect)) {
		/* This is not even the right size. */
		retval = NONCE_BAD;
		goto leave;
	}
	if (constant_memequal(expect, nonce, noncelen)) {
		/* Not what we would have signed earlier */
		retval = NONCE_BAD;
		goto leave;
	}

	/*
	 * By how many seconds is this nonce stale?  Negative value
	 * would mean it was issued by another server with its clock
	 * skewed in the future.
	 */
	ostamp = parse_timestamp(push_cert_nonce, NULL, 10);
	nonce_stamp_slop = (long)ostamp - (long)stamp;

	if (nonce_stamp_slop_limit &&
	    labs(nonce_stamp_slop) <= nonce_stamp_slop_limit) {
		/*
		 * Pretend as if the received nonce (which passes the
		 * HMAC check, so it is not a forged by third-party)
		 * is what we issued.
		 */
		free((void *)push_cert_nonce);
		push_cert_nonce = xstrdup(nonce);
		retval = NONCE_OK;
	} else {
		retval = NONCE_SLOP;
	}

leave:
	free(nonce);
	free(expect);
	return retval;
}

/*
 * Return 1 if there is no push_cert or if the push options in push_cert are
 * the same as those in the argument; 0 otherwise.
 */
static int check_cert_push_options(const struct string_list *push_options)
{
	const char *buf = push_cert.buf;

	const char *option;
	size_t optionlen;
	int options_seen = 0;

	int retval = 1;

	if (!*buf)
		return 1;

	while ((option = find_commit_header(buf, "push-option", &optionlen))) {
		buf = option + optionlen + 1;
		options_seen++;
		if (options_seen > push_options->nr
		    || xstrncmpz(push_options->items[options_seen - 1].string,
				 option, optionlen))
			return 0;
	}

	if (options_seen != push_options->nr)
		retval = 0;

	return retval;
}

static void prepare_push_cert_sha1(struct child_process *proc)
{
	static int already_done;

	if (!push_cert.len)
		return;

	if (!already_done) {
		int bogs /* beginning_of_gpg_sig */;

		already_done = 1;
		if (write_object_file(push_cert.buf, push_cert.len, OBJ_BLOB,
				      &push_cert_oid))
			oidclr(&push_cert_oid, the_repository->hash_algo);

		memset(&sigcheck, '\0', sizeof(sigcheck));

		bogs = parse_signed_buffer(push_cert.buf, push_cert.len);
		sigcheck.payload = xmemdupz(push_cert.buf, bogs);
		sigcheck.payload_len = bogs;
		check_signature(&sigcheck, push_cert.buf + bogs,
				push_cert.len - bogs);

		nonce_status = check_nonce(sigcheck.payload);
	}
	if (!is_null_oid(&push_cert_oid)) {
		strvec_pushf(&proc->env, "GIT_PUSH_CERT=%s",
			     oid_to_hex(&push_cert_oid));
		strvec_pushf(&proc->env, "GIT_PUSH_CERT_SIGNER=%s",
			     sigcheck.signer ? sigcheck.signer : "");
		strvec_pushf(&proc->env, "GIT_PUSH_CERT_KEY=%s",
			     sigcheck.key ? sigcheck.key : "");
		strvec_pushf(&proc->env, "GIT_PUSH_CERT_STATUS=%c",
			     sigcheck.result);
		if (push_cert_nonce) {
			strvec_pushf(&proc->env,
				     "GIT_PUSH_CERT_NONCE=%s",
				     push_cert_nonce);
			strvec_pushf(&proc->env,
				     "GIT_PUSH_CERT_NONCE_STATUS=%s",
				     nonce_status);
			if (nonce_status == NONCE_SLOP)
				strvec_pushf(&proc->env,
					     "GIT_PUSH_CERT_NONCE_SLOP=%ld",
					     nonce_stamp_slop);
		}
	}
}

struct receive_hook_feed_state {
	struct command *cmd;
	struct ref_push_report *report;
	int skip_broken;
	struct strbuf buf;
	const struct string_list *push_options;
};

typedef int (*feed_fn)(void *, const char **, size_t *);
static int run_and_feed_hook(const char *hook_name, feed_fn feed,
			     struct receive_hook_feed_state *feed_state)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	struct async muxer;
	int code;
	const char *hook_path = find_hook(the_repository, hook_name);

	if (!hook_path)
		return 0;

	strvec_push(&proc.args, hook_path);
	proc.in = -1;
	proc.stdout_to_stderr = 1;
	proc.trace2_hook_name = hook_name;

	if (feed_state->push_options) {
		size_t i;
		for (i = 0; i < feed_state->push_options->nr; i++)
			strvec_pushf(&proc.env,
				     "GIT_PUSH_OPTION_%"PRIuMAX"=%s",
				     (uintmax_t)i,
				     feed_state->push_options->items[i].string);
		strvec_pushf(&proc.env, "GIT_PUSH_OPTION_COUNT=%"PRIuMAX"",
			     (uintmax_t)feed_state->push_options->nr);
	} else
		strvec_pushf(&proc.env, "GIT_PUSH_OPTION_COUNT");

	if (tmp_objdir)
		strvec_pushv(&proc.env, tmp_objdir_env(tmp_objdir));

	if (use_sideband) {
		memset(&muxer, 0, sizeof(muxer));
		muxer.proc = copy_to_sideband;
		muxer.in = -1;
		code = start_async(&muxer);
		if (code)
			return code;
		proc.err = muxer.in;
	}

	prepare_push_cert_sha1(&proc);

	code = start_command(&proc);
	if (code) {
		if (use_sideband)
			finish_async(&muxer);
		return code;
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	while (1) {
		const char *buf;
		size_t n;
		if (feed(feed_state, &buf, &n))
			break;
		if (write_in_full(proc.in, buf, n) < 0)
			break;
	}
	close(proc.in);
	if (use_sideband)
		finish_async(&muxer);

	sigchain_pop(SIGPIPE);

	return finish_command(&proc);
}

static int feed_receive_hook(void *state_, const char **bufp, size_t *sizep)
{
	struct receive_hook_feed_state *state = state_;
	struct command *cmd = state->cmd;

	while (cmd &&
	       state->skip_broken && (cmd->error_string || cmd->did_not_exist))
		cmd = cmd->next;
	if (!cmd)
		return -1; /* EOF */
	if (!bufp)
		return 0; /* OK, can feed something. */
	strbuf_reset(&state->buf);
	if (!state->report)
		state->report = cmd->report;
	if (state->report) {
		struct object_id *old_oid;
		struct object_id *new_oid;
		const char *ref_name;

		old_oid = state->report->old_oid ? state->report->old_oid : &cmd->old_oid;
		new_oid = state->report->new_oid ? state->report->new_oid : &cmd->new_oid;
		ref_name = state->report->ref_name ? state->report->ref_name : cmd->ref_name;
		strbuf_addf(&state->buf, "%s %s %s\n",
			    oid_to_hex(old_oid), oid_to_hex(new_oid),
			    ref_name);
		state->report = state->report->next;
		if (!state->report)
			state->cmd = cmd->next;
	} else {
		strbuf_addf(&state->buf, "%s %s %s\n",
			    oid_to_hex(&cmd->old_oid), oid_to_hex(&cmd->new_oid),
			    cmd->ref_name);
		state->cmd = cmd->next;
	}
	if (bufp) {
		*bufp = state->buf.buf;
		*sizep = state->buf.len;
	}
	return 0;
}

static int run_receive_hook(struct command *commands,
			    const char *hook_name,
			    int skip_broken,
			    const struct string_list *push_options)
{
	struct receive_hook_feed_state state;
	int status;

	strbuf_init(&state.buf, 0);
	state.cmd = commands;
	state.skip_broken = skip_broken;
	state.report = NULL;
	if (feed_receive_hook(&state, NULL, NULL))
		return 0;
	state.cmd = commands;
	state.push_options = push_options;
	status = run_and_feed_hook(hook_name, feed_receive_hook, &state);
	strbuf_release(&state.buf);
	return status;
}

static int run_update_hook(struct command *cmd)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	int code;
	const char *hook_path = find_hook(the_repository, "update");

	if (!hook_path)
		return 0;

	strvec_push(&proc.args, hook_path);
	strvec_push(&proc.args, cmd->ref_name);
	strvec_push(&proc.args, oid_to_hex(&cmd->old_oid));
	strvec_push(&proc.args, oid_to_hex(&cmd->new_oid));

	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;
	proc.err = use_sideband ? -1 : 0;
	proc.trace2_hook_name = "update";

	code = start_command(&proc);
	if (code)
		return code;
	if (use_sideband)
		copy_to_sideband(proc.err, -1, NULL);
	return finish_command(&proc);
}

static struct command *find_command_by_refname(struct command *list,
					       const char *refname)
{
	for (; list; list = list->next)
		if (!strcmp(list->ref_name, refname))
			return list;
	return NULL;
}

static int read_proc_receive_report(struct packet_reader *reader,
				    struct command *commands,
				    struct strbuf *errmsg)
{
	struct command *cmd;
	struct command *hint = NULL;
	struct ref_push_report *report = NULL;
	int new_report = 0;
	int code = 0;
	int once = 0;
	int response = 0;

	for (;;) {
		struct object_id old_oid, new_oid;
		const char *head;
		const char *refname;
		char *p;
		enum packet_read_status status;

		status = packet_reader_read(reader);
		if (status != PACKET_READ_NORMAL) {
			/* Check whether proc-receive exited abnormally */
			if (status == PACKET_READ_EOF && !response) {
				strbuf_addstr(errmsg, "proc-receive exited abnormally");
				return -1;
			}
			break;
		}
		response++;

		head = reader->line;
		p = strchr(head, ' ');
		if (!p) {
			strbuf_addf(errmsg, "proc-receive reported incomplete status line: '%s'\n", head);
			code = -1;
			continue;
		}
		*p++ = '\0';
		if (!strcmp(head, "option")) {
			const char *key, *val;

			if (!hint || !(report || new_report)) {
				if (!once++)
					strbuf_addstr(errmsg, "proc-receive reported 'option' without a matching 'ok/ng' directive\n");
				code = -1;
				continue;
			}
			if (new_report) {
				if (!hint->report) {
					CALLOC_ARRAY(hint->report, 1);
					report = hint->report;
				} else {
					report = hint->report;
					while (report->next)
						report = report->next;
					report->next = xcalloc(1, sizeof(struct ref_push_report));
					report = report->next;
				}
				new_report = 0;
			}
			key = p;
			p = strchr(key, ' ');
			if (p)
				*p++ = '\0';
			val = p;
			if (!strcmp(key, "refname"))
				report->ref_name = xstrdup_or_null(val);
			else if (!strcmp(key, "old-oid") && val &&
				 !parse_oid_hex(val, &old_oid, &val))
				report->old_oid = oiddup(&old_oid);
			else if (!strcmp(key, "new-oid") && val &&
				 !parse_oid_hex(val, &new_oid, &val))
				report->new_oid = oiddup(&new_oid);
			else if (!strcmp(key, "forced-update"))
				report->forced_update = 1;
			else if (!strcmp(key, "fall-through"))
				/* Fall through, let 'receive-pack' to execute it. */
				hint->run_proc_receive = 0;
			continue;
		}

		report = NULL;
		new_report = 0;
		refname = p;
		p = strchr(refname, ' ');
		if (p)
			*p++ = '\0';
		if (strcmp(head, "ok") && strcmp(head, "ng")) {
			strbuf_addf(errmsg, "proc-receive reported bad status '%s' on ref '%s'\n",
				    head, refname);
			code = -1;
			continue;
		}

		/* first try searching at our hint, falling back to all refs */
		if (hint)
			hint = find_command_by_refname(hint, refname);
		if (!hint)
			hint = find_command_by_refname(commands, refname);
		if (!hint) {
			strbuf_addf(errmsg, "proc-receive reported status on unknown ref: %s\n",
				    refname);
			code = -1;
			continue;
		}
		if (!hint->run_proc_receive) {
			strbuf_addf(errmsg, "proc-receive reported status on unexpected ref: %s\n",
				    refname);
			code = -1;
			continue;
		}
		hint->run_proc_receive |= RUN_PROC_RECEIVE_RETURNED;
		if (!strcmp(head, "ng")) {
			if (p)
				hint->error_string = hint->error_string_owned = xstrdup(p);
			else
				hint->error_string = "failed";
			code = -1;
			continue;
		}
		new_report = 1;
	}

	for (cmd = commands; cmd; cmd = cmd->next)
		if (cmd->run_proc_receive && !cmd->error_string &&
		    !(cmd->run_proc_receive & RUN_PROC_RECEIVE_RETURNED)) {
		    cmd->error_string = "proc-receive failed to report status";
		    code = -1;
		}
	return code;
}

static int run_proc_receive_hook(struct command *commands,
				 const struct string_list *push_options)
{
	struct child_process proc = CHILD_PROCESS_INIT;
	struct async muxer;
	struct command *cmd;
	struct packet_reader reader;
	struct strbuf cap = STRBUF_INIT;
	struct strbuf errmsg = STRBUF_INIT;
	int hook_use_push_options = 0;
	int version = 0;
	int code;
	const char *hook_path = find_hook(the_repository, "proc-receive");

	if (!hook_path) {
		rp_error("cannot find hook 'proc-receive'");
		return -1;
	}

	strvec_push(&proc.args, hook_path);
	proc.in = -1;
	proc.out = -1;
	proc.trace2_hook_name = "proc-receive";

	if (use_sideband) {
		memset(&muxer, 0, sizeof(muxer));
		muxer.proc = copy_to_sideband;
		muxer.in = -1;
		code = start_async(&muxer);
		if (code)
			return code;
		proc.err = muxer.in;
	} else {
		proc.err = 0;
	}

	code = start_command(&proc);
	if (code) {
		if (use_sideband)
			finish_async(&muxer);
		return code;
	}

	sigchain_push(SIGPIPE, SIG_IGN);

	/* Version negotiaton */
	packet_reader_init(&reader, proc.out, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_GENTLE_ON_EOF);
	if (use_atomic)
		strbuf_addstr(&cap, " atomic");
	if (use_push_options)
		strbuf_addstr(&cap, " push-options");
	if (cap.len) {
		code = packet_write_fmt_gently(proc.in, "version=1%c%s\n", '\0', cap.buf + 1);
		strbuf_release(&cap);
	} else {
		code = packet_write_fmt_gently(proc.in, "version=1\n");
	}
	if (!code)
		code = packet_flush_gently(proc.in);

	if (!code)
		for (;;) {
			int linelen;
			enum packet_read_status status;

			status = packet_reader_read(&reader);
			if (status != PACKET_READ_NORMAL) {
				/* Check whether proc-receive exited abnormally */
				if (status == PACKET_READ_EOF)
					code = -1;
				break;
			}

			if (reader.pktlen > 8 && starts_with(reader.line, "version=")) {
				version = atoi(reader.line + 8);
				linelen = strlen(reader.line);
				if (linelen < reader.pktlen) {
					const char *feature_list = reader.line + linelen + 1;
					if (parse_feature_request(feature_list, "push-options"))
						hook_use_push_options = 1;
				}
			}
		}

	if (code) {
		strbuf_addstr(&errmsg, "fail to negotiate version with proc-receive hook");
		goto cleanup;
	}

	switch (version) {
	case 0:
		/* fallthrough */
	case 1:
		break;
	default:
		strbuf_addf(&errmsg, "proc-receive version '%d' is not supported",
			    version);
		code = -1;
		goto cleanup;
	}

	/* Send commands */
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!cmd->run_proc_receive || cmd->skip_update || cmd->error_string)
			continue;
		code = packet_write_fmt_gently(proc.in, "%s %s %s",
					       oid_to_hex(&cmd->old_oid),
					       oid_to_hex(&cmd->new_oid),
					       cmd->ref_name);
		if (code)
			break;
	}
	if (!code)
		code = packet_flush_gently(proc.in);
	if (code) {
		strbuf_addstr(&errmsg, "fail to write commands to proc-receive hook");
		goto cleanup;
	}

	/* Send push options */
	if (hook_use_push_options) {
		struct string_list_item *item;

		for_each_string_list_item(item, push_options) {
			code = packet_write_fmt_gently(proc.in, "%s", item->string);
			if (code)
				break;
		}
		if (!code)
			code = packet_flush_gently(proc.in);
		if (code) {
			strbuf_addstr(&errmsg,
				      "fail to write push-options to proc-receive hook");
			goto cleanup;
		}
	}

	/* Read result from proc-receive */
	code = read_proc_receive_report(&reader, commands, &errmsg);

cleanup:
	close(proc.in);
	close(proc.out);
	if (use_sideband)
		finish_async(&muxer);
	if (finish_command(&proc))
		code = -1;
	if (errmsg.len >0) {
		char *p = errmsg.buf;

		p += errmsg.len - 1;
		if (*p == '\n')
			*p = '\0';
		rp_error("%s", errmsg.buf);
		strbuf_release(&errmsg);
	}
	sigchain_pop(SIGPIPE);

	return code;
}

static const char *refuse_unconfigured_deny_msg =
	N_("By default, updating the current branch in a non-bare repository\n"
	   "is denied, because it will make the index and work tree inconsistent\n"
	   "with what you pushed, and will require 'git reset --hard' to match\n"
	   "the work tree to HEAD.\n"
	   "\n"
	   "You can set the 'receive.denyCurrentBranch' configuration variable\n"
	   "to 'ignore' or 'warn' in the remote repository to allow pushing into\n"
	   "its current branch; however, this is not recommended unless you\n"
	   "arranged to update its work tree to match what you pushed in some\n"
	   "other way.\n"
	   "\n"
	   "To squelch this message and still keep the default behaviour, set\n"
	   "'receive.denyCurrentBranch' configuration variable to 'refuse'.");

static void refuse_unconfigured_deny(void)
{
	rp_error("%s", _(refuse_unconfigured_deny_msg));
}

static const char *refuse_unconfigured_deny_delete_current_msg =
	N_("By default, deleting the current branch is denied, because the next\n"
	   "'git clone' won't result in any file checked out, causing confusion.\n"
	   "\n"
	   "You can set 'receive.denyDeleteCurrent' configuration variable to\n"
	   "'warn' or 'ignore' in the remote repository to allow deleting the\n"
	   "current branch, with or without a warning message.\n"
	   "\n"
	   "To squelch this message, you can set it to 'refuse'.");

static void refuse_unconfigured_deny_delete_current(void)
{
	rp_error("%s", _(refuse_unconfigured_deny_delete_current_msg));
}

static const struct object_id *command_singleton_iterator(void *cb_data);
static int update_shallow_ref(struct command *cmd, struct shallow_info *si)
{
	struct shallow_lock shallow_lock = SHALLOW_LOCK_INIT;
	struct oid_array extra = OID_ARRAY_INIT;
	struct check_connected_options opt = CHECK_CONNECTED_INIT;
	uint32_t mask = 1 << (cmd->index % 32);
	int i;

	trace_printf_key(&trace_shallow,
			 "shallow: update_shallow_ref %s\n", cmd->ref_name);
	for (i = 0; i < si->shallow->nr; i++)
		if (si->used_shallow[i] &&
		    (si->used_shallow[i][cmd->index / 32] & mask) &&
		    !delayed_reachability_test(si, i))
			oid_array_append(&extra, &si->shallow->oid[i]);

	opt.env = tmp_objdir_env(tmp_objdir);
	setup_alternate_shallow(&shallow_lock, &opt.shallow_file, &extra);
	if (check_connected(command_singleton_iterator, cmd, &opt)) {
		rollback_shallow_file(the_repository, &shallow_lock);
		oid_array_clear(&extra);
		return -1;
	}

	commit_shallow_file(the_repository, &shallow_lock);

	/*
	 * Make sure setup_alternate_shallow() for the next ref does
	 * not lose these new roots..
	 */
	for (i = 0; i < extra.nr; i++)
		register_shallow(the_repository, &extra.oid[i]);

	si->shallow_ref[cmd->index] = 0;
	oid_array_clear(&extra);
	return 0;
}

/*
 * NEEDSWORK: we should consolidate various implementations of "are we
 * on an unborn branch?" test into one, and make the unified one more
 * robust. !get_sha1() based check used here and elsewhere would not
 * allow us to tell an unborn branch from corrupt ref, for example.
 * For the purpose of fixing "deploy-to-update does not work when
 * pushing into an empty repository" issue, this should suffice for
 * now.
 */
static int head_has_history(void)
{
	struct object_id oid;

	return !repo_get_oid(the_repository, "HEAD", &oid);
}

static const char *push_to_deploy(unsigned char *sha1,
				  struct strvec *env,
				  const char *work_tree)
{
	struct child_process child = CHILD_PROCESS_INIT;

	strvec_pushl(&child.args, "update-index", "-q", "--ignore-submodules",
		     "--refresh", NULL);
	strvec_pushv(&child.env, env->v);
	child.dir = work_tree;
	child.no_stdin = 1;
	child.stdout_to_stderr = 1;
	child.git_cmd = 1;
	if (run_command(&child))
		return "Up-to-date check failed";

	/* run_command() does not clean up completely; reinitialize */
	child_process_init(&child);
	strvec_pushl(&child.args, "diff-files", "--quiet",
		     "--ignore-submodules", "--", NULL);
	strvec_pushv(&child.env, env->v);
	child.dir = work_tree;
	child.no_stdin = 1;
	child.stdout_to_stderr = 1;
	child.git_cmd = 1;
	if (run_command(&child))
		return "Working directory has unstaged changes";

	child_process_init(&child);
	strvec_pushl(&child.args, "diff-index", "--quiet", "--cached",
		     "--ignore-submodules",
		     /* diff-index with either HEAD or an empty tree */
		     head_has_history() ? "HEAD" : empty_tree_oid_hex(the_repository->hash_algo),
		     "--", NULL);
	strvec_pushv(&child.env, env->v);
	child.no_stdin = 1;
	child.no_stdout = 1;
	child.stdout_to_stderr = 0;
	child.git_cmd = 1;
	if (run_command(&child))
		return "Working directory has staged changes";

	child_process_init(&child);
	strvec_pushl(&child.args, "read-tree", "-u", "-m", hash_to_hex(sha1),
		     NULL);
	strvec_pushv(&child.env, env->v);
	child.dir = work_tree;
	child.no_stdin = 1;
	child.no_stdout = 1;
	child.stdout_to_stderr = 0;
	child.git_cmd = 1;
	if (run_command(&child))
		return "Could not update working tree to new HEAD";

	return NULL;
}

static const char *push_to_checkout_hook = "push-to-checkout";

static const char *push_to_checkout(unsigned char *hash,
				    int *invoked_hook,
				    struct strvec *env,
				    const char *work_tree)
{
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;
	opt.invoked_hook = invoked_hook;

	strvec_pushf(env, "GIT_WORK_TREE=%s", absolute_path(work_tree));
	strvec_pushv(&opt.env, env->v);
	strvec_push(&opt.args, hash_to_hex(hash));
	if (run_hooks_opt(the_repository, push_to_checkout_hook, &opt))
		return "push-to-checkout hook declined";
	else
		return NULL;
}

static const char *update_worktree(unsigned char *sha1, const struct worktree *worktree)
{
	const char *retval;
	char *git_dir;
	struct strvec env = STRVEC_INIT;
	int invoked_hook;

	if (!worktree || !worktree->path)
		BUG("worktree->path must be non-NULL");

	if (worktree->is_bare)
		return "denyCurrentBranch = updateInstead needs a worktree";
	git_dir = get_worktree_git_dir(worktree);

	strvec_pushf(&env, "GIT_DIR=%s", absolute_path(git_dir));

	retval = push_to_checkout(sha1, &invoked_hook, &env, worktree->path);
	if (!invoked_hook)
		retval = push_to_deploy(sha1, &env, worktree->path);

	strvec_clear(&env);
	free(git_dir);
	return retval;
}

static const char *update(struct command *cmd, struct shallow_info *si)
{
	const char *name = cmd->ref_name;
	struct strbuf namespaced_name_buf = STRBUF_INIT;
	static char *namespaced_name;
	const char *ret;
	struct object_id *old_oid = &cmd->old_oid;
	struct object_id *new_oid = &cmd->new_oid;
	int do_update_worktree = 0;
	struct worktree **worktrees = get_worktrees();
	const struct worktree *worktree =
		find_shared_symref(worktrees, "HEAD", name);

	/* only refs/... are allowed */
	if (!starts_with(name, "refs/") ||
	    check_refname_format(name + 5, is_null_oid(new_oid) ?
				 REFNAME_ALLOW_ONELEVEL : 0)) {
		rp_error("refusing to update funny ref '%s' remotely", name);
		ret = "funny refname";
		goto out;
	}

	strbuf_addf(&namespaced_name_buf, "%s%s", get_git_namespace(), name);
	free(namespaced_name);
	namespaced_name = strbuf_detach(&namespaced_name_buf, NULL);

	if (worktree && !worktree->is_bare) {
		switch (deny_current_branch) {
		case DENY_IGNORE:
			break;
		case DENY_WARN:
			rp_warning("updating the current branch");
			break;
		case DENY_REFUSE:
		case DENY_UNCONFIGURED:
			rp_error("refusing to update checked out branch: %s", name);
			if (deny_current_branch == DENY_UNCONFIGURED)
				refuse_unconfigured_deny();
			ret = "branch is currently checked out";
			goto out;
		case DENY_UPDATE_INSTEAD:
			/* pass -- let other checks intervene first */
			do_update_worktree = 1;
			break;
		}
	}

	if (!is_null_oid(new_oid) &&
	    !odb_has_object(the_repository->objects, new_oid,
			    HAS_OBJECT_RECHECK_PACKED | HAS_OBJECT_FETCH_PROMISOR)) {
		error("unpack should have generated %s, "
		      "but I can't find it!", oid_to_hex(new_oid));
		ret = "bad pack";
		goto out;
	}

	if (!is_null_oid(old_oid) && is_null_oid(new_oid)) {
		if (deny_deletes && starts_with(name, "refs/heads/")) {
			rp_error("denying ref deletion for %s", name);
			ret = "deletion prohibited";
			goto out;
		}

		if (worktree || (head_name && !strcmp(namespaced_name, head_name))) {
			switch (deny_delete_current) {
			case DENY_IGNORE:
				break;
			case DENY_WARN:
				rp_warning("deleting the current branch");
				break;
			case DENY_REFUSE:
			case DENY_UNCONFIGURED:
			case DENY_UPDATE_INSTEAD:
				if (deny_delete_current == DENY_UNCONFIGURED)
					refuse_unconfigured_deny_delete_current();
				rp_error("refusing to delete the current branch: %s", name);
				ret = "deletion of the current branch prohibited";
				goto out;
			default:
				ret = "Invalid denyDeleteCurrent setting";
				goto out;
			}
		}
	}

	if (deny_non_fast_forwards && !is_null_oid(new_oid) &&
	    !is_null_oid(old_oid) &&
	    starts_with(name, "refs/heads/")) {
		struct object *old_object, *new_object;
		struct commit *old_commit, *new_commit;
		int ret2;

		old_object = parse_object(the_repository, old_oid);
		new_object = parse_object(the_repository, new_oid);

		if (!old_object || !new_object ||
		    old_object->type != OBJ_COMMIT ||
		    new_object->type != OBJ_COMMIT) {
			error("bad sha1 objects for %s", name);
			ret = "bad ref";
			goto out;
		}
		old_commit = (struct commit *)old_object;
		new_commit = (struct commit *)new_object;
		ret2 = repo_in_merge_bases(the_repository, old_commit, new_commit);
		if (ret2 < 0)
			exit(128);
		if (!ret2) {
			rp_error("denying non-fast-forward %s"
				 " (you should pull first)", name);
			ret = "non-fast-forward";
			goto out;
		}
	}
	if (run_update_hook(cmd)) {
		rp_error("hook declined to update %s", name);
		ret = "hook declined";
		goto out;
	}

	if (do_update_worktree) {
		ret = update_worktree(new_oid->hash, worktree);
		if (ret)
			goto out;
	}

	if (is_null_oid(new_oid)) {
		struct strbuf err = STRBUF_INIT;
		if (!parse_object(the_repository, old_oid)) {
			old_oid = NULL;
			if (refs_ref_exists(get_main_ref_store(the_repository), name)) {
				rp_warning("allowing deletion of corrupt ref");
			} else {
				rp_warning("deleting a non-existent ref");
				cmd->did_not_exist = 1;
			}
		}
		if (ref_transaction_delete(transaction,
					   namespaced_name,
					   old_oid,
					   NULL, 0,
					   "push", &err)) {
			rp_error("%s", err.buf);
			ret = "failed to delete";
		} else {
			ret = NULL; /* good */
		}
		strbuf_release(&err);
	}
	else {
		struct strbuf err = STRBUF_INIT;
		if (shallow_update && si->shallow_ref[cmd->index] &&
		    update_shallow_ref(cmd, si)) {
			ret = "shallow error";
			goto out;
		}

		if (ref_transaction_update(transaction,
					   namespaced_name,
					   new_oid, old_oid,
					   NULL, NULL,
					   0, "push",
					   &err)) {
			rp_error("%s", err.buf);
			ret = "failed to update ref";
		} else {
			ret = NULL; /* good */
		}
		strbuf_release(&err);
	}

out:
	free_worktrees(worktrees);
	return ret;
}

static void run_update_post_hook(struct command *commands)
{
	struct command *cmd;
	struct child_process proc = CHILD_PROCESS_INIT;
	const char *hook;

	hook = find_hook(the_repository, "post-update");
	if (!hook)
		return;

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (cmd->error_string || cmd->did_not_exist)
			continue;
		if (!proc.args.nr)
			strvec_push(&proc.args, hook);
		strvec_push(&proc.args, cmd->ref_name);
	}
	if (!proc.args.nr)
		return;

	proc.no_stdin = 1;
	proc.stdout_to_stderr = 1;
	proc.err = use_sideband ? -1 : 0;
	proc.trace2_hook_name = "post-update";

	if (!start_command(&proc)) {
		if (use_sideband)
			copy_to_sideband(proc.err, -1, NULL);
		finish_command(&proc);
	}
}

static void check_aliased_update_internal(struct command *cmd,
					  struct string_list *list,
					  const char *dst_name, int flag)
{
	struct string_list_item *item;
	struct command *dst_cmd;

	if (!(flag & REF_ISSYMREF))
		return;

	if (!dst_name) {
		rp_error("refusing update to broken symref '%s'", cmd->ref_name);
		cmd->skip_update = 1;
		cmd->error_string = "broken symref";
		return;
	}
	dst_name = strip_namespace(dst_name);

	if (!(item = string_list_lookup(list, dst_name)))
		return;

	cmd->skip_update = 1;

	dst_cmd = (struct command *) item->util;

	if (oideq(&cmd->old_oid, &dst_cmd->old_oid) &&
	    oideq(&cmd->new_oid, &dst_cmd->new_oid))
		return;

	dst_cmd->skip_update = 1;

	rp_error("refusing inconsistent update between symref '%s' (%s..%s) and"
		 " its target '%s' (%s..%s)",
		 cmd->ref_name,
		 repo_find_unique_abbrev(the_repository, &cmd->old_oid, DEFAULT_ABBREV),
		 repo_find_unique_abbrev(the_repository, &cmd->new_oid, DEFAULT_ABBREV),
		 dst_cmd->ref_name,
		 repo_find_unique_abbrev(the_repository, &dst_cmd->old_oid, DEFAULT_ABBREV),
		 repo_find_unique_abbrev(the_repository, &dst_cmd->new_oid, DEFAULT_ABBREV));

	cmd->error_string = dst_cmd->error_string =
		"inconsistent aliased update";
}

static void check_aliased_update(struct command *cmd, struct string_list *list)
{
	struct strbuf buf = STRBUF_INIT;
	const char *dst_name;
	int flag;

	strbuf_addf(&buf, "%s%s", get_git_namespace(), cmd->ref_name);
	dst_name = refs_resolve_ref_unsafe(get_main_ref_store(the_repository),
					   buf.buf, 0, NULL, &flag);
	check_aliased_update_internal(cmd, list, dst_name, flag);
	strbuf_release(&buf);
}

static void check_aliased_updates(struct command *commands)
{
	struct command *cmd;
	struct string_list ref_list = STRING_LIST_INIT_NODUP;

	for (cmd = commands; cmd; cmd = cmd->next) {
		struct string_list_item *item =
			string_list_append(&ref_list, cmd->ref_name);
		item->util = (void *)cmd;
	}
	string_list_sort(&ref_list);

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!cmd->error_string)
			check_aliased_update(cmd, &ref_list);
	}

	string_list_clear(&ref_list, 0);
}

static const struct object_id *command_singleton_iterator(void *cb_data)
{
	struct command **cmd_list = cb_data;
	struct command *cmd = *cmd_list;

	if (!cmd || is_null_oid(&cmd->new_oid))
		return NULL;
	*cmd_list = NULL; /* this returns only one */
	return &cmd->new_oid;
}

static void set_connectivity_errors(struct command *commands,
				    struct shallow_info *si)
{
	struct command *cmd;

	for (cmd = commands; cmd; cmd = cmd->next) {
		struct command *singleton = cmd;
		struct check_connected_options opt = CHECK_CONNECTED_INIT;

		if (shallow_update && si->shallow_ref[cmd->index])
			/* to be checked in update_shallow_ref() */
			continue;

		opt.env = tmp_objdir_env(tmp_objdir);
		if (!check_connected(command_singleton_iterator, &singleton,
				     &opt))
			continue;

		cmd->error_string = "missing necessary objects";
	}
}

struct iterate_data {
	struct command *cmds;
	struct shallow_info *si;
};

static const struct object_id *iterate_receive_command_list(void *cb_data)
{
	struct iterate_data *data = cb_data;
	struct command **cmd_list = &data->cmds;
	struct command *cmd = *cmd_list;

	for (; cmd; cmd = cmd->next) {
		if (shallow_update && data->si->shallow_ref[cmd->index])
			/* to be checked in update_shallow_ref() */
			continue;
		if (!is_null_oid(&cmd->new_oid) && !cmd->skip_update) {
			*cmd_list = cmd->next;
			return &cmd->new_oid;
		}
	}
	return NULL;
}

static void reject_updates_to_hidden(struct command *commands)
{
	struct strbuf refname_full = STRBUF_INIT;
	size_t prefix_len;
	struct command *cmd;

	strbuf_addstr(&refname_full, get_git_namespace());
	prefix_len = refname_full.len;

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (cmd->error_string)
			continue;

		strbuf_setlen(&refname_full, prefix_len);
		strbuf_addstr(&refname_full, cmd->ref_name);

		if (!ref_is_hidden(cmd->ref_name, refname_full.buf, &hidden_refs))
			continue;
		if (is_null_oid(&cmd->new_oid))
			cmd->error_string = "deny deleting a hidden ref";
		else
			cmd->error_string = "deny updating a hidden ref";
	}

	strbuf_release(&refname_full);
}

static int should_process_cmd(struct command *cmd)
{
	return !cmd->error_string && !cmd->skip_update;
}

static void BUG_if_skipped_connectivity_check(struct command *commands,
					       struct shallow_info *si)
{
	struct command *cmd;

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (should_process_cmd(cmd) && si->shallow_ref[cmd->index])
			bug("connectivity check has not been run on ref %s",
			    cmd->ref_name);
	}
	BUG_if_bug("connectivity check skipped???");
}

static void ref_transaction_rejection_handler(const char *refname,
					      const struct object_id *old_oid UNUSED,
					      const struct object_id *new_oid UNUSED,
					      const char *old_target UNUSED,
					      const char *new_target UNUSED,
					      enum ref_transaction_error err,
					      void *cb_data)
{
	struct strmap *failed_refs = cb_data;

	strmap_put(failed_refs, refname, (char *)ref_transaction_error_msg(err));
}

static void execute_commands_non_atomic(struct command *commands,
					struct shallow_info *si)
{
	struct command *cmd;
	struct strbuf err = STRBUF_INIT;
	const char *reported_error = NULL;
	struct strmap failed_refs = STRMAP_INIT;

	transaction = ref_store_transaction_begin(get_main_ref_store(the_repository),
						  REF_TRANSACTION_ALLOW_FAILURE, &err);
	if (!transaction) {
		rp_error("%s", err.buf);
		strbuf_reset(&err);
		reported_error = "transaction failed to start";
		goto failure;
	}

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!should_process_cmd(cmd) || cmd->run_proc_receive)
			continue;

		cmd->error_string = update(cmd, si);
	}

	if (ref_transaction_commit(transaction, &err)) {
		rp_error("%s", err.buf);
		reported_error = "failed to update refs";
		goto failure;
	}

	ref_transaction_for_each_rejected_update(transaction,
						 ref_transaction_rejection_handler,
						 &failed_refs);

	if (strmap_empty(&failed_refs))
		goto cleanup;

failure:
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (reported_error)
			cmd->error_string = reported_error;
		else if (strmap_contains(&failed_refs, cmd->ref_name))
			cmd->error_string = strmap_get(&failed_refs, cmd->ref_name);
	}

cleanup:
	ref_transaction_free(transaction);
	strmap_clear(&failed_refs, 0);
	strbuf_release(&err);
}

static void execute_commands_atomic(struct command *commands,
					struct shallow_info *si)
{
	struct command *cmd;
	struct strbuf err = STRBUF_INIT;
	const char *reported_error = "atomic push failure";

	transaction = ref_store_transaction_begin(get_main_ref_store(the_repository),
						  0, &err);
	if (!transaction) {
		rp_error("%s", err.buf);
		strbuf_reset(&err);
		reported_error = "transaction failed to start";
		goto failure;
	}

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!should_process_cmd(cmd) || cmd->run_proc_receive)
			continue;

		cmd->error_string = update(cmd, si);

		if (cmd->error_string)
			goto failure;
	}

	if (ref_transaction_commit(transaction, &err)) {
		rp_error("%s", err.buf);
		reported_error = "atomic transaction failed";
		goto failure;
	}
	goto cleanup;

failure:
	for (cmd = commands; cmd; cmd = cmd->next)
		if (!cmd->error_string)
			cmd->error_string = reported_error;

cleanup:
	ref_transaction_free(transaction);
	strbuf_release(&err);
}

static void execute_commands(struct command *commands,
			     const char *unpacker_error,
			     struct shallow_info *si,
			     const struct string_list *push_options)
{
	struct check_connected_options opt = CHECK_CONNECTED_INIT;
	struct command *cmd;
	struct iterate_data data;
	struct async muxer;
	int err_fd = 0;
	int run_proc_receive = 0;

	if (unpacker_error) {
		for (cmd = commands; cmd; cmd = cmd->next)
			cmd->error_string = "unpacker error";
		return;
	}

	if (!skip_connectivity_check) {
		if (use_sideband) {
			memset(&muxer, 0, sizeof(muxer));
			muxer.proc = copy_to_sideband;
			muxer.in = -1;
			if (!start_async(&muxer))
				err_fd = muxer.in;
			/* ...else, continue without relaying sideband */
		}

		data.cmds = commands;
		data.si = si;
		opt.err_fd = err_fd;
		opt.progress = err_fd && !quiet;
		opt.env = tmp_objdir_env(tmp_objdir);
		opt.exclude_hidden_refs_section = "receive";

		if (check_connected(iterate_receive_command_list, &data, &opt))
			set_connectivity_errors(commands, si);

		if (use_sideband)
			finish_async(&muxer);
	}

	reject_updates_to_hidden(commands);

	/*
	 * Try to find commands that have special prefix in their reference names,
	 * and mark them to run an external "proc-receive" hook later.
	 */
	if (proc_receive_ref) {
		for (cmd = commands; cmd; cmd = cmd->next) {
			if (!should_process_cmd(cmd))
				continue;

			if (proc_receive_ref_matches(cmd)) {
				cmd->run_proc_receive = RUN_PROC_RECEIVE_SCHEDULED;
				run_proc_receive = 1;
			}
		}
	}

	if (run_receive_hook(commands, "pre-receive", 0, push_options)) {
		for (cmd = commands; cmd; cmd = cmd->next) {
			if (!cmd->error_string)
				cmd->error_string = "pre-receive hook declined";
		}
		return;
	}

	/*
	 * If there is no command ready to run, should return directly to destroy
	 * temporary data in the quarantine area.
	 */
	for (cmd = commands; cmd && cmd->error_string; cmd = cmd->next)
		; /* nothing */
	if (!cmd)
		return;

	/*
	 * Now we'll start writing out refs, which means the objects need
	 * to be in their final positions so that other processes can see them.
	 */
	if (tmp_objdir_migrate(tmp_objdir) < 0) {
		for (cmd = commands; cmd; cmd = cmd->next) {
			if (!cmd->error_string)
				cmd->error_string = "unable to migrate objects to permanent storage";
		}
		return;
	}
	tmp_objdir = NULL;

	check_aliased_updates(commands);

	free(head_name_to_free);
	head_name = head_name_to_free = refs_resolve_refdup(get_main_ref_store(the_repository),
							    "HEAD", 0, NULL,
							    NULL);

	if (run_proc_receive &&
	    run_proc_receive_hook(commands, push_options))
		for (cmd = commands; cmd; cmd = cmd->next)
			if (!cmd->error_string &&
			    !(cmd->run_proc_receive & RUN_PROC_RECEIVE_RETURNED) &&
			    (cmd->run_proc_receive || use_atomic))
				cmd->error_string = "fail to run proc-receive hook";

	if (use_atomic)
		execute_commands_atomic(commands, si);
	else
		execute_commands_non_atomic(commands, si);

	if (shallow_update)
		BUG_if_skipped_connectivity_check(commands, si);
}

static struct command **queue_command(struct command **tail,
				      const char *line,
				      int linelen)
{
	struct object_id old_oid, new_oid;
	struct command *cmd;
	const char *refname;
	int reflen;
	const char *p;

	if (parse_oid_hex(line, &old_oid, &p) ||
	    *p++ != ' ' ||
	    parse_oid_hex(p, &new_oid, &p) ||
	    *p++ != ' ')
		die("protocol error: expected old/new/ref, got '%s'", line);

	refname = p;
	reflen = linelen - (p - line);
	FLEX_ALLOC_MEM(cmd, ref_name, refname, reflen);
	oidcpy(&cmd->old_oid, &old_oid);
	oidcpy(&cmd->new_oid, &new_oid);
	*tail = cmd;
	return &cmd->next;
}

static void free_commands(struct command *commands)
{
	while (commands) {
		struct command *next = commands->next;

		ref_push_report_free(commands->report);
		free(commands->error_string_owned);
		free(commands);
		commands = next;
	}
}

static void queue_commands_from_cert(struct command **tail,
				     struct strbuf *push_cert)
{
	const char *boc, *eoc;

	if (*tail)
		die("protocol error: got both push certificate and unsigned commands");

	boc = strstr(push_cert->buf, "\n\n");
	if (!boc)
		die("malformed push certificate %.*s", 100, push_cert->buf);
	else
		boc += 2;
	eoc = push_cert->buf + parse_signed_buffer(push_cert->buf, push_cert->len);

	while (boc < eoc) {
		const char *eol = memchr(boc, '\n', eoc - boc);
		tail = queue_command(tail, boc, eol ? eol - boc : eoc - boc);
		boc = eol ? eol + 1 : eoc;
	}
}

static struct command *read_head_info(struct packet_reader *reader,
				      struct oid_array *shallow)
{
	struct command *commands = NULL;
	struct command **p = &commands;
	for (;;) {
		int linelen;

		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		if (reader->pktlen > 8 && starts_with(reader->line, "shallow ")) {
			struct object_id oid;
			if (get_oid_hex(reader->line + 8, &oid))
				die("protocol error: expected shallow sha, got '%s'",
				    reader->line + 8);
			oid_array_append(shallow, &oid);
			continue;
		}

		linelen = strlen(reader->line);
		if (linelen < reader->pktlen) {
			const char *feature_list = reader->line + linelen + 1;
			const char *hash = NULL;
			const char *client_sid;
			size_t len = 0;
			if (parse_feature_request(feature_list, "report-status"))
				report_status = 1;
			if (parse_feature_request(feature_list, "report-status-v2"))
				report_status_v2 = 1;
			if (parse_feature_request(feature_list, "side-band-64k"))
				use_sideband = LARGE_PACKET_MAX;
			if (parse_feature_request(feature_list, "quiet"))
				quiet = 1;
			if (advertise_atomic_push
			    && parse_feature_request(feature_list, "atomic"))
				use_atomic = 1;
			if (advertise_push_options
			    && parse_feature_request(feature_list, "push-options"))
				use_push_options = 1;
			hash = parse_feature_value(feature_list, "object-format", &len, NULL);
			if (!hash) {
				hash = hash_algos[GIT_HASH_SHA1].name;
				len = strlen(hash);
			}
			if (xstrncmpz(the_hash_algo->name, hash, len))
				die("error: unsupported object format '%s'", hash);
			client_sid = parse_feature_value(feature_list, "session-id", &len, NULL);
			if (client_sid) {
				char *sid = xstrndup(client_sid, len);
				trace2_data_string("transfer", NULL, "client-sid", client_sid);
				free(sid);
			}
		}

		if (!strcmp(reader->line, "push-cert")) {
			int true_flush = 0;
			int saved_options = reader->options;
			reader->options &= ~PACKET_READ_CHOMP_NEWLINE;

			for (;;) {
				packet_reader_read(reader);
				if (reader->status == PACKET_READ_FLUSH) {
					true_flush = 1;
					break;
				}
				if (reader->status != PACKET_READ_NORMAL) {
					die("protocol error: got an unexpected packet");
				}
				if (!strcmp(reader->line, "push-cert-end\n"))
					break; /* end of cert */
				strbuf_addstr(&push_cert, reader->line);
			}
			reader->options = saved_options;

			if (true_flush)
				break;
			continue;
		}

		p = queue_command(p, reader->line, linelen);
	}

	if (push_cert.len)
		queue_commands_from_cert(p, &push_cert);

	return commands;
}

static void read_push_options(struct packet_reader *reader,
			      struct string_list *options)
{
	while (1) {
		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		string_list_append(options, reader->line);
	}
}

static const char *parse_pack_header(struct pack_header *hdr)
{
	switch (read_pack_header(0, hdr)) {
	case PH_ERROR_EOF:
		return "eof before pack header was fully read";

	case PH_ERROR_PACK_SIGNATURE:
		return "protocol error (pack signature mismatch detected)";

	case PH_ERROR_PROTOCOL:
		return "protocol error (pack version unsupported)";

	default:
		return "unknown error in parse_pack_header";

	case 0:
		return NULL;
	}
}

static struct tempfile *pack_lockfile;

static void push_header_arg(struct strvec *args, struct pack_header *hdr)
{
	strvec_pushf(args, "--pack_header=%"PRIu32",%"PRIu32,
		     ntohl(hdr->hdr_version), ntohl(hdr->hdr_entries));
}

static const char *unpack(int err_fd, struct shallow_info *si)
{
	struct pack_header hdr;
	const char *hdr_err;
	int status;
	struct child_process child = CHILD_PROCESS_INIT;
	int fsck_objects = (receive_fsck_objects >= 0
			    ? receive_fsck_objects
			    : transfer_fsck_objects >= 0
			    ? transfer_fsck_objects
			    : 0);

	hdr_err = parse_pack_header(&hdr);
	if (hdr_err) {
		if (err_fd > 0)
			close(err_fd);
		return hdr_err;
	}

	if (si->nr_ours || si->nr_theirs) {
		alt_shallow_file = setup_temporary_shallow(si->shallow);
		strvec_push(&child.args, "--shallow-file");
		strvec_push(&child.args, alt_shallow_file);
	}

	tmp_objdir = tmp_objdir_create(the_repository, "incoming");
	if (!tmp_objdir) {
		if (err_fd > 0)
			close(err_fd);
		return "unable to create temporary object directory";
	}
	strvec_pushv(&child.env, tmp_objdir_env(tmp_objdir));

	/*
	 * Normally we just pass the tmp_objdir environment to the child
	 * processes that do the heavy lifting, but we may need to see these
	 * objects ourselves to set up shallow information.
	 */
	tmp_objdir_add_as_alternate(tmp_objdir);

	if (ntohl(hdr.hdr_entries) < unpack_limit) {
		strvec_push(&child.args, "unpack-objects");
		push_header_arg(&child.args, &hdr);
		if (quiet)
			strvec_push(&child.args, "-q");
		if (fsck_objects)
			strvec_pushf(&child.args, "--strict%s",
				     fsck_msg_types.buf);
		if (max_input_size)
			strvec_pushf(&child.args, "--max-input-size=%"PRIuMAX,
				     (uintmax_t)max_input_size);
		child.no_stdout = 1;
		child.err = err_fd;
		child.git_cmd = 1;
		status = run_command(&child);
		if (status)
			return "unpack-objects abnormal exit";
	} else {
		char hostname[HOST_NAME_MAX + 1];
		char *lockfile;

		strvec_pushl(&child.args, "index-pack", "--stdin", NULL);
		push_header_arg(&child.args, &hdr);

		if (xgethostname(hostname, sizeof(hostname)))
			xsnprintf(hostname, sizeof(hostname), "localhost");
		strvec_pushf(&child.args,
			     "--keep=receive-pack %"PRIuMAX" on %s",
			     (uintmax_t)getpid(),
			     hostname);

		if (!quiet && err_fd)
			strvec_push(&child.args, "--show-resolving-progress");
		if (use_sideband)
			strvec_push(&child.args, "--report-end-of-input");
		if (fsck_objects)
			strvec_pushf(&child.args, "--strict%s",
				     fsck_msg_types.buf);
		if (!reject_thin)
			strvec_push(&child.args, "--fix-thin");
		if (max_input_size)
			strvec_pushf(&child.args, "--max-input-size=%"PRIuMAX,
				     (uintmax_t)max_input_size);
		child.out = -1;
		child.err = err_fd;
		child.git_cmd = 1;
		status = start_command(&child);
		if (status)
			return "index-pack fork failed";

		lockfile = index_pack_lockfile(the_repository, child.out, NULL);
		if (lockfile) {
			pack_lockfile = register_tempfile(lockfile);
			free(lockfile);
		}
		close(child.out);

		status = finish_command(&child);
		if (status)
			return "index-pack abnormal exit";
		reprepare_packed_git(the_repository);
	}
	return NULL;
}

static const char *unpack_with_sideband(struct shallow_info *si)
{
	struct async muxer;
	const char *ret;

	if (!use_sideband)
		return unpack(0, si);

	use_keepalive = KEEPALIVE_AFTER_NUL;
	memset(&muxer, 0, sizeof(muxer));
	muxer.proc = copy_to_sideband;
	muxer.in = -1;
	if (start_async(&muxer))
		return NULL;

	ret = unpack(muxer.in, si);

	finish_async(&muxer);
	return ret;
}

static void prepare_shallow_update(struct shallow_info *si)
{
	int i, j, k, bitmap_size = DIV_ROUND_UP(si->ref->nr, 32);

	ALLOC_ARRAY(si->used_shallow, si->shallow->nr);
	assign_shallow_commits_to_refs(si, si->used_shallow, NULL);

	CALLOC_ARRAY(si->need_reachability_test, si->shallow->nr);
	CALLOC_ARRAY(si->reachable, si->shallow->nr);
	CALLOC_ARRAY(si->shallow_ref, si->ref->nr);

	for (i = 0; i < si->nr_ours; i++)
		si->need_reachability_test[si->ours[i]] = 1;

	for (i = 0; i < si->shallow->nr; i++) {
		if (!si->used_shallow[i])
			continue;
		for (j = 0; j < bitmap_size; j++) {
			if (!si->used_shallow[i][j])
				continue;
			si->need_reachability_test[i]++;
			for (k = 0; k < 32; k++)
				if (si->used_shallow[i][j] & (1U << k))
					si->shallow_ref[j * 32 + k]++;
		}

		/*
		 * true for those associated with some refs and belong
		 * in "ours" list aka "step 7 not done yet"
		 */
		si->need_reachability_test[i] =
			si->need_reachability_test[i] > 1;
	}

	/*
	 * keep hooks happy by forcing a temporary shallow file via
	 * env variable because we can't add --shallow-file to every
	 * command. check_connected() will be done with
	 * true .git/shallow though.
	 */
	setenv(GIT_SHALLOW_FILE_ENVIRONMENT, alt_shallow_file, 1);
}

static void update_shallow_info(struct command *commands,
				struct shallow_info *si,
				struct oid_array *ref)
{
	struct command *cmd;
	int *ref_status;
	remove_nonexistent_theirs_shallow(si);
	if (!si->nr_ours && !si->nr_theirs) {
		shallow_update = 0;
		return;
	}

	for (cmd = commands; cmd; cmd = cmd->next) {
		if (is_null_oid(&cmd->new_oid))
			continue;
		oid_array_append(ref, &cmd->new_oid);
		cmd->index = ref->nr - 1;
	}
	si->ref = ref;

	if (shallow_update) {
		prepare_shallow_update(si);
		return;
	}

	ALLOC_ARRAY(ref_status, ref->nr);
	assign_shallow_commits_to_refs(si, NULL, ref_status);
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (is_null_oid(&cmd->new_oid))
			continue;
		if (ref_status[cmd->index]) {
			cmd->error_string = "shallow update not allowed";
			cmd->skip_update = 1;
		}
	}
	free(ref_status);
}

static void report(struct command *commands, const char *unpack_status)
{
	struct command *cmd;
	struct strbuf buf = STRBUF_INIT;

	packet_buf_write(&buf, "unpack %s\n",
			 unpack_status ? unpack_status : "ok");
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!cmd->error_string)
			packet_buf_write(&buf, "ok %s\n",
					 cmd->ref_name);
		else
			packet_buf_write(&buf, "ng %s %s\n",
					 cmd->ref_name, cmd->error_string);
	}
	packet_buf_flush(&buf);

	if (use_sideband)
		send_sideband(1, 1, buf.buf, buf.len, use_sideband);
	else
		write_or_die(1, buf.buf, buf.len);
	strbuf_release(&buf);
}

static void report_v2(struct command *commands, const char *unpack_status)
{
	struct command *cmd;
	struct strbuf buf = STRBUF_INIT;
	struct ref_push_report *report;

	packet_buf_write(&buf, "unpack %s\n",
			 unpack_status ? unpack_status : "ok");
	for (cmd = commands; cmd; cmd = cmd->next) {
		int count = 0;

		if (cmd->error_string) {
			packet_buf_write(&buf, "ng %s %s\n",
					 cmd->ref_name,
					 cmd->error_string);
			continue;
		}
		packet_buf_write(&buf, "ok %s\n",
				 cmd->ref_name);
		for (report = cmd->report; report; report = report->next) {
			if (count++ > 0)
				packet_buf_write(&buf, "ok %s\n",
						 cmd->ref_name);
			if (report->ref_name)
				packet_buf_write(&buf, "option refname %s\n",
						 report->ref_name);
			if (report->old_oid)
				packet_buf_write(&buf, "option old-oid %s\n",
						 oid_to_hex(report->old_oid));
			if (report->new_oid)
				packet_buf_write(&buf, "option new-oid %s\n",
						 oid_to_hex(report->new_oid));
			if (report->forced_update)
				packet_buf_write(&buf, "option forced-update\n");
		}
	}
	packet_buf_flush(&buf);

	if (use_sideband)
		send_sideband(1, 1, buf.buf, buf.len, use_sideband);
	else
		write_or_die(1, buf.buf, buf.len);
	strbuf_release(&buf);
}

static int delete_only(struct command *commands)
{
	struct command *cmd;
	for (cmd = commands; cmd; cmd = cmd->next) {
		if (!is_null_oid(&cmd->new_oid))
			return 0;
	}
	return 1;
}

int cmd_receive_pack(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	int advertise_refs = 0;
	struct command *commands;
	struct oid_array shallow = OID_ARRAY_INIT;
	struct oid_array ref = OID_ARRAY_INIT;
	struct shallow_info si;
	struct packet_reader reader;

	struct option options[] = {
		OPT__QUIET(&quiet, N_("quiet")),
		OPT_HIDDEN_BOOL(0, "skip-connectivity-check", &skip_connectivity_check, NULL),
		OPT_HIDDEN_BOOL(0, "stateless-rpc", &stateless_rpc, NULL),
		OPT_HIDDEN_BOOL(0, "http-backend-info-refs", &advertise_refs, NULL),
		OPT_ALIAS(0, "advertise-refs", "http-backend-info-refs"),
		OPT_HIDDEN_BOOL(0, "reject-thin-pack-for-testing", &reject_thin, NULL),
		OPT_END()
	};

	packet_trace_identity("receive-pack");

	argc = parse_options(argc, argv, prefix, options, receive_pack_usage, 0);

	if (argc > 1)
		usage_msg_opt(_("too many arguments"), receive_pack_usage, options);
	if (argc == 0)
		usage_msg_opt(_("you must specify a directory"), receive_pack_usage, options);

	service_dir = argv[0];

	setup_path();

	if (!enter_repo(service_dir, 0))
		die("'%s' does not appear to be a git repository", service_dir);

	git_config(receive_pack_config, NULL);
	if (cert_nonce_seed)
		push_cert_nonce = prepare_push_cert_nonce(service_dir, time(NULL));

	if (0 <= receive_unpack_limit)
		unpack_limit = receive_unpack_limit;
	else if (0 <= transfer_unpack_limit)
		unpack_limit = transfer_unpack_limit;

	switch (determine_protocol_version_server()) {
	case protocol_v2:
		/*
		 * push support for protocol v2 has not been implemented yet,
		 * so ignore the request to use v2 and fallback to using v0.
		 */
		break;
	case protocol_v1:
		/*
		 * v1 is just the original protocol with a version string,
		 * so just fall through after writing the version string.
		 */
		if (advertise_refs || !stateless_rpc)
			packet_write_fmt(1, "version 1\n");

		/* fallthrough */
	case protocol_v0:
		break;
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	if (advertise_refs || !stateless_rpc) {
		write_head_info();
	}
	if (advertise_refs)
		return 0;

	packet_reader_init(&reader, 0, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	if ((commands = read_head_info(&reader, &shallow))) {
		const char *unpack_status = NULL;
		struct string_list push_options = STRING_LIST_INIT_DUP;

		if (use_push_options)
			read_push_options(&reader, &push_options);
		if (!check_cert_push_options(&push_options)) {
			struct command *cmd;
			for (cmd = commands; cmd; cmd = cmd->next)
				cmd->error_string = "inconsistent push options";
		}

		prepare_shallow_info(&si, &shallow);
		if (!si.nr_ours && !si.nr_theirs)
			shallow_update = 0;
		if (!delete_only(commands)) {
			unpack_status = unpack_with_sideband(&si);
			update_shallow_info(commands, &si, &ref);
		}
		use_keepalive = KEEPALIVE_ALWAYS;
		execute_commands(commands, unpack_status, &si,
				 &push_options);
		delete_tempfile(&pack_lockfile);
		sigchain_push(SIGPIPE, SIG_IGN);
		if (report_status_v2)
			report_v2(commands, unpack_status);
		else if (report_status)
			report(commands, unpack_status);
		sigchain_pop(SIGPIPE);
		run_receive_hook(commands, "post-receive", 1,
				 &push_options);
		run_update_post_hook(commands);
		free_commands(commands);
		string_list_clear(&push_options, 0);
		if (auto_gc) {
			struct child_process proc = CHILD_PROCESS_INIT;

			if (prepare_auto_maintenance(1, &proc)) {
				proc.no_stdin = 1;
				proc.stdout_to_stderr = 1;
				proc.err = use_sideband ? -1 : 0;

				if (!start_command(&proc)) {
					if (use_sideband)
						copy_to_sideband(proc.err, -1, NULL);
					finish_command(&proc);
				}
			}
		}
		if (auto_update_server_info)
			update_server_info(the_repository, 0);
		clear_shallow_info(&si);
	}
	if (use_sideband)
		packet_flush(1);
	oid_array_clear(&shallow);
	oid_array_clear(&ref);
	strvec_clear(&hidden_refs);
	free((void *)push_cert_nonce);
	return 0;
}
