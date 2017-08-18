#include "cache.h"
#include "mru.h"

char *odb_pack_name(struct strbuf *buf,
		    const unsigned char *sha1,
		    const char *ext)
{
	strbuf_reset(buf);
	strbuf_addf(buf, "%s/pack/pack-%s.%s", get_object_directory(),
		    sha1_to_hex(sha1), ext);
	return buf->buf;
}

char *sha1_pack_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;
	return odb_pack_name(&buf, sha1, "pack");
}

char *sha1_pack_index_name(const unsigned char *sha1)
{
	static struct strbuf buf = STRBUF_INIT;
	return odb_pack_name(&buf, sha1, "idx");
}

unsigned int pack_used_ctr;
unsigned int pack_mmap_calls;
unsigned int peak_pack_open_windows;
unsigned int pack_open_windows;
unsigned int pack_open_fds;
unsigned int pack_max_fds;
size_t peak_pack_mapped;
size_t pack_mapped;
struct packed_git *packed_git;

static struct mru packed_git_mru_storage;
struct mru *packed_git_mru = &packed_git_mru_storage;

#define SZ_FMT PRIuMAX
static inline uintmax_t sz_fmt(size_t s) { return s; }

void pack_report(void)
{
	fprintf(stderr,
		"pack_report: getpagesize()            = %10" SZ_FMT "\n"
		"pack_report: core.packedGitWindowSize = %10" SZ_FMT "\n"
		"pack_report: core.packedGitLimit      = %10" SZ_FMT "\n",
		sz_fmt(getpagesize()),
		sz_fmt(packed_git_window_size),
		sz_fmt(packed_git_limit));
	fprintf(stderr,
		"pack_report: pack_used_ctr            = %10u\n"
		"pack_report: pack_mmap_calls          = %10u\n"
		"pack_report: pack_open_windows        = %10u / %10u\n"
		"pack_report: pack_mapped              = "
			"%10" SZ_FMT " / %10" SZ_FMT "\n",
		pack_used_ctr,
		pack_mmap_calls,
		pack_open_windows, peak_pack_open_windows,
		sz_fmt(pack_mapped), sz_fmt(peak_pack_mapped));
}
