#include "config.h"
#include "list.h"
#include "strbuf.h"
<<<<<<< HEAD
<<<<<<< HEAD
=======
#include "strvec.h"
>>>>>>> upstream/seen
=======
#include "strvec.h"
>>>>>>> upstream/seen

struct hook
{
	struct list_head list;
	enum config_scope origin;
	struct strbuf command;
};

struct list_head* hook_list(const struct strbuf *hookname);
<<<<<<< HEAD
=======
int run_hooks(const char *const *env, const struct strbuf *hookname,
	      const struct strvec *args);
<<<<<<< HEAD
>>>>>>> upstream/seen
=======
>>>>>>> upstream/seen

void free_hook(struct hook *ptr);
void clear_hook_list(void);
