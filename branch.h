#ifndef BRANCH_H
#define BRANCH_H

void create_branch(const char *head,
		   const char *name, const char *start_name,
		   int force, int reflog, int track);

#endif
