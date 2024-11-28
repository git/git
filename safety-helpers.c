#include "safety-protocol.h"
#include "cache.h"
#include "dir.h"
#include "repository.h"
#include "path.h"
#include "refs.h"
#include "commit.h"
#include "diff.h"
#include "diffcore.h"
#include "revision.h"

/* Check if repository has uncommitted changes */
int has_uncommitted_changes(struct repository *repo)
{
    struct rev_info revs;
    int result = 0;
    
    if (!repo)
        return 0;
        
    repo_init_revisions(repo, &revs, NULL);
    init_revisions(&revs, NULL);
    
    if (read_cache_preload(repo->index, NULL) < 0)
        return 0;
        
    if (repo_read_index(repo) < 0)
        return 0;
        
    /* Check for unstaged changes */
    if (repo->index->cache_changed)
        result = 1;
        
    /* Check for untracked files */
    if (repo->index->untracked_nr > 0)
        result = 1;
        
    release_revisions(&revs);
    return result;
}

/* Check if repository has staged changes */
int has_staged_changes(struct repository *repo)
{
    struct rev_info revs;
    int result = 0;
    
    if (!repo)
        return 0;
        
    repo_init_revisions(repo, &revs, NULL);
    init_revisions(&revs, NULL);
    
    if (read_cache_preload(repo->index, NULL) < 0)
        return 0;
        
    if (repo_read_index(repo) < 0)
        return 0;
        
    /* Check for staged changes */
    if (repo->index->cache_changed)
        result = 1;
        
    release_revisions(&revs);
    return result;
}

/* Check if path is inside git directory */
int is_inside_git_dir(struct repository *repo)
{
    struct strbuf path = STRBUF_INIT;
    int result = 0;
    
    if (!repo || !repo->gitdir)
        return 0;
        
    strbuf_addstr(&path, repo->gitdir);
    
    if (is_git_directory(path.buf))
        result = 1;
        
    strbuf_release(&path);
    return result;
}
