#include "config.h"
#include "list.h"
#include "strbuf.h"

struct hook
{
	struct list_head list;
	enum config_scope origin;
	struct strbuf command;
};

struct list_head* hook_list(const struct strbuf *hookname);

void free_hook(struct hook *ptr);
void clear_hook_list(void);
