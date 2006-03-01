#include <assert.h>

#include "cache.h"
#include "refs.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"

#define DEBUG 0

struct commit** blame_lines;
int num_blame_lines;

struct util_info
{
    int* line_map;
    int num_lines;
    unsigned char sha1[20]; /* blob sha, not commit! */
    char* buf;
    unsigned long size;
//    const char* path;
};

struct chunk
{
    int off1, len1; // ---
    int off2, len2; // +++
};

struct patch
{
    struct chunk* chunks;
    int num;
};

static void get_blob(struct commit* commit);

int num_get_patch = 0;
int num_commits = 0;

struct patch* get_patch(struct commit* commit, struct commit* other)
{
    struct patch* ret = xmalloc(sizeof(struct patch));
    ret->chunks = NULL;
    ret->num = 0;

    struct util_info* info_c = (struct util_info*) commit->object.util;
    struct util_info* info_o = (struct util_info*) other->object.util;

    if(!memcmp(info_c->sha1, info_o->sha1, 20))
        return ret;

    get_blob(commit);
    get_blob(other);

    FILE* fout = fopen("/tmp/git-blame-tmp1", "w");
    if(!fout)
        die("fopen tmp1 failed: %s", strerror(errno));

    if(fwrite(info_c->buf, info_c->size, 1, fout) != 1)
        die("fwrite 1 failed: %s", strerror(errno));
    fclose(fout);

    fout = fopen("/tmp/git-blame-tmp2", "w");
    if(!fout)
        die("fopen tmp2 failed: %s", strerror(errno));

    if(fwrite(info_o->buf, info_o->size, 1, fout) != 1)
        die("fwrite 2 failed: %s", strerror(errno));
    fclose(fout);

    FILE* fin = popen("diff -u0 /tmp/git-blame-tmp1 /tmp/git-blame-tmp2", "r");
    if(!fin)
        die("popen failed: %s", strerror(errno));

    char buf[1024];
    while(fgets(buf, sizeof(buf), fin)) {
        if(buf[0] != '@' || buf[1] != '@')
            continue;

        if(DEBUG)
            printf("chunk line: %s", buf);
        ret->num++;
        ret->chunks = xrealloc(ret->chunks, sizeof(struct chunk)*ret->num);
        struct chunk* chunk = &ret->chunks[ret->num-1];

        assert(!strncmp(buf, "@@ -", 4));

        char* start = buf+4;
        char* sp = index(start, ' ');
        *sp = '\0';
        if(index(start, ',')) {
            int ret = sscanf(start, "%d,%d", &chunk->off1, &chunk->len1);
            assert(ret == 2);
        } else {
            int ret = sscanf(start, "%d", &chunk->off1);
            assert(ret == 1);
            chunk->len1 = 1;
        }
        *sp = ' ';

        start = sp+1;
        sp = index(start, ' ');
        *sp = '\0';
        if(index(start, ',')) {
            int ret = sscanf(start, "%d,%d", &chunk->off2, &chunk->len2);
            assert(ret == 2);
        } else {
            int ret = sscanf(start, "%d", &chunk->off2);
            assert(ret == 1);
            chunk->len2 = 1;
        }
        *sp = ' ';

        if(chunk->off1 > 0)
            chunk->off1 -= 1;
        if(chunk->off2 > 0)
            chunk->off2 -= 1;

        assert(chunk->off1 >= 0);
        assert(chunk->off2 >= 0);
    }
    fclose(fin);

    num_get_patch++;
    return ret;
}

void free_patch(struct patch* p)
{
    free(p->chunks);
    free(p);
}

static int get_blob_sha1_internal(unsigned char *sha1, const char *base, int baselen,
                                  const char *pathname, unsigned mode, int stage);


static unsigned char blob_sha1[20];
static int get_blob_sha1(struct tree* t, const char* pathname, unsigned char* sha1)
{
    const char *pathspec[2];
    pathspec[0] = pathname;
    pathspec[1] = NULL;
    memset(blob_sha1, 0, sizeof(blob_sha1));
    read_tree_recursive(t, "", 0, 0, pathspec, get_blob_sha1_internal);

    int i;
    for(i = 0; i < 20; i++) {
        if(blob_sha1[i] != 0)
            break;
    }

    if(i == 20)
        return -1;

    memcpy(sha1, blob_sha1, 20);
    return 0;
}

static int get_blob_sha1_internal(unsigned char *sha1, const char *base, int baselen,
                                  const char *pathname, unsigned mode, int stage)
{
//    printf("Got blob: %s base: '%s' baselen: %d pathname: '%s' mode: %o stage: %d\n",
//           sha1_to_hex(sha1), base, baselen, pathname, mode, stage);

    if(S_ISDIR(mode))
        return READ_TREE_RECURSIVE;

    memcpy(blob_sha1, sha1, 20);
    return -1;
}

static void get_blob(struct commit* commit)
{
    struct util_info* info = commit->object.util;
    char type[20];

    if(info->buf)
        return;

    info->buf = read_sha1_file(info->sha1, type, &info->size);
    assert(!strcmp(type, "blob"));
}

void print_patch(struct patch* p)
{
    printf("Num chunks: %d\n", p->num);
    int i;
    for(i = 0; i < p->num; i++) {
        printf("%d,%d %d,%d\n", p->chunks[i].off1, p->chunks[i].len1, p->chunks[i].off2, p->chunks[i].len2);
    }
}


// p is a patch from commit to other.
void fill_line_map(struct commit* commit, struct commit* other, struct patch* p)
{
    int num_lines = ((struct util_info*) commit->object.util)->num_lines;
    int* line_map = ((struct util_info*) commit->object.util)->line_map;
    int num_lines2 = ((struct util_info*) other->object.util)->num_lines;
    int* line_map2 = ((struct util_info*) other->object.util)->line_map;
    int cur_chunk = 0;
    int i1, i2;

    if(p->num && DEBUG)
        print_patch(p);

    for(i1 = 0; i1 < num_lines; i1++)
        line_map[i1] = -1;

    if(DEBUG)
        printf("num lines 1: %d num lines 2: %d\n", num_lines, num_lines2);

    for(i1 = 0, i2 = 0; i1 < num_lines; i1++, i2++) {
        if(DEBUG > 1)
            printf("%d %d\n", i1, i2);

        if(i2 >= num_lines2)
            break;

        line_map[i1] = line_map2[i2];

        struct chunk* chunk = NULL;
        if(cur_chunk < p->num)
            chunk = &p->chunks[cur_chunk];

        if(chunk && chunk->off1 == i1) {
            i2 = chunk->off2;

            if(chunk->len1 > 0)
                i1 += chunk->len1-1;
            if(chunk->len2 > 0)
                i2 += chunk->len2-1;
            cur_chunk++;
        }
    }
}

int map_line(struct commit* commit, int line)
{
    struct util_info* info = commit->object.util;
    assert(line >= 0 && line < info->num_lines);
    return info->line_map[line];
}

int fill_util_info(struct commit* commit, const char* path)
{
    if(commit->object.util)
        return 0;

    struct util_info* util = xmalloc(sizeof(struct util_info));
    util->buf = NULL;
    util->size = 0;
    util->num_lines = -1;
    util->line_map = NULL;

    commit->object.util = util;

    if(get_blob_sha1(commit->tree, path, util->sha1))
        return -1;

    return 0;
}

void alloc_line_map(struct commit* commit)
{
    struct util_info* util = commit->object.util;

    if(util->line_map)
        return;

    get_blob(commit);

    int i;
    util->num_lines = 0;
    for(i = 0; i < util->size; i++) {
        if(util->buf[i] == '\n')
            util->num_lines++;
    }
    util->line_map = xmalloc(sizeof(int)*util->num_lines);
}

void copy_line_map(struct commit* dst, struct commit* src)
{
    struct util_info* u_dst = dst->object.util;
    struct util_info* u_src = src->object.util;

    u_dst->line_map = u_src->line_map;
    u_dst->num_lines = u_src->num_lines;
    u_dst->buf = u_src->buf;
    u_dst->size = u_src->size;
}

void process_commits(struct commit_list* list, const char* path)
{
    int i;

    while(list) {
        struct commit* commit = pop_commit(&list);
        struct commit_list* parents;
        struct util_info* info;

        info = commit->object.util;
        num_commits++;
        if(DEBUG)
            printf("\nProcessing commit: %d %s\n", num_commits, sha1_to_hex(commit->object.sha1));
        for(parents = commit->parents;
            parents != NULL; parents = parents->next) {
            struct commit* parent = parents->item;

            if(parse_commit(parent) < 0)
                die("parse_commit error");

            if(DEBUG)
                printf("parent: %s\n", sha1_to_hex(parent->object.sha1));

            if(fill_util_info(parent, path))
                continue;

            // Temporarily assign everything to the parent.
            int num_blame = 0;
            for(i = 0; i < num_blame_lines; i++) {
                if(blame_lines[i] == commit) {
                    num_blame++;
                    blame_lines[i] = parent;
                }
            }

            if(num_blame == 0)
                continue;

            struct patch* patch = get_patch(parent, commit);
            if(patch->num == 0) {
                copy_line_map(parent, commit);
            } else {
                alloc_line_map(parent);
                fill_line_map(parent, commit, patch);
            }

            for(i = 0; i < patch->num; i++) {
                int l;
                for(l = 0; l < patch->chunks[i].len2; l++) {
                    int mapped_line = map_line(commit, patch->chunks[i].off2 + l);
                    if(mapped_line != -1 && blame_lines[mapped_line] == parent)
                        blame_lines[mapped_line] = commit;
                }
            }
            free_patch(patch);
        }
    }
}

#define SEEN 1
struct commit_list* get_commit_list(struct commit* commit, const char* pathname)
{
    struct commit_list* ret = NULL;
    struct commit_list* process = NULL;
    unsigned char sha1[20];

    commit_list_insert(commit, &process);

    while(process) {
        struct commit* com = pop_commit(&process);
        if(com->object.flags & SEEN)
            continue;

        com->object.flags |= SEEN;
        commit_list_insert(com, &ret);
        struct commit_list* parents;

        parse_commit(com);

        for(parents = com->parents;
            parents != NULL; parents = parents->next) {
            struct commit* parent = parents->item;

            parse_commit(parent);

            if(!get_blob_sha1(parent->tree, pathname, sha1))
                commit_list_insert(parent, &process);
        }
    }

    return ret;
}

int main(int argc, const char **argv)
{
    unsigned char sha1[20];
    struct commit *commit;
    const char* filename;
    int i;

    setup_git_directory();

    if (argc != 3)
        die("Usage: blame commit-ish file");

    if (get_sha1(argv[1], sha1))
        die("get_sha1 failed");

    commit = lookup_commit_reference(sha1);

    filename = argv[2];

    struct commit_list* list = get_commit_list(commit, filename);
    sort_in_topological_order(&list, 1);

    if(fill_util_info(commit, filename)) {
        printf("%s not found in %s\n", filename, argv[1]);
        return 0;
    }
    alloc_line_map(commit);

    struct util_info* util = commit->object.util;
    num_blame_lines = util->num_lines;
    blame_lines = xmalloc(sizeof(struct commit*)*num_blame_lines);


    for(i = 0; i < num_blame_lines; i++) {
        blame_lines[i] = commit;

        ((struct util_info*) commit->object.util)->line_map[i] = i;
    }

    process_commits(list, filename);

    for(i = 0; i < num_blame_lines; i++) {
        printf("%d %s\n", i+1-1, sha1_to_hex(blame_lines[i]->object.sha1));
//        printf("%d %s\n", i+1-1, find_unique_abbrev(blame_lines[i]->object.sha1, 6));
    }

    if(DEBUG) {
        printf("num get patch: %d\n", num_get_patch);
        printf("num commits: %d\n", num_commits);
    }

    return 0;
}
