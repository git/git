#ifndef HELP_H
#define HELP_H

struct cmdnames {
	int alloc;
	int cnt;
	struct cmdname {
		size_t len; /* also used for similarity index in help.c */
		char name[FLEX_ARRAY];
	} **names;
};

static inline void mput_char(char c, unsigned int num)
{
	while(num--)
		putchar(c);
}

extern void list_common_cmds_help(void);
extern const char *help_unknown_cmd(const char *cmd);
extern void load_command_list(const char *prefix,
			      struct cmdnames *main_cmds,
			      struct cmdnames *other_cmds);
extern void add_cmdname(struct cmdnames *cmds, const char *name, int len);
/* Here we require that excludes is a sorted list. */
extern void exclude_cmds(struct cmdnames *cmds, struct cmdnames *excludes);
extern int is_in_cmdlist(struct cmdnames *cmds, const char *name);
extern void list_commands(unsigned int colopts, struct cmdnames *main_cmds, struct cmdnames *other_cmds);

/*
 * call this to die(), when it is suspected that the user mistyped a
 * ref to the command, to give suggested "correct" refs.
 */
extern void help_unknown_ref(const char *ref, const char *cmd, const char *error);
#endif /* HELP_H */
