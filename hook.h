#include "config.h"
#include "list.h"
#include "strbuf.h"
#include "argv-array.h"

struct hook
{
	struct list_head list;
	enum config_scope origin;
	struct strbuf command;
};

struct list_head* hook_list(const struct strbuf *hookname);
int run_hooks(const char *const *env, const struct strbuf *hookname,
	      const struct argv_array *args);

void free_hook(struct hook *ptr);
void clear_hook_list(void);
