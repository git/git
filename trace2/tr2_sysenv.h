#ifndef TR2_SYSENV_H
#define TR2_SYSENV_H

/*
 * The Trace2 settings that can be loaded from /etc/gitconfig
 * and/or user environment variables.
 *
 * Note that this set does not contain any of the transient
 * environment variables used to pass information from parent
 * to child git processes, such "GIT_TRACE2_PARENT_SID".
 */
enum tr2_sysenv_variable {
	TR2_SYSENV_CFG_PARAM = 0,

	TR2_SYSENV_DST_DEBUG,

	TR2_SYSENV_NORMAL,
	TR2_SYSENV_NORMAL_BRIEF,

	TR2_SYSENV_EVENT,
	TR2_SYSENV_EVENT_BRIEF,
	TR2_SYSENV_EVENT_NESTING,

	TR2_SYSENV_PERF,
	TR2_SYSENV_PERF_BRIEF,

	TR2_SYSENV_MUST_BE_LAST
};

void tr2_sysenv_load(void);

const char *tr2_sysenv_get(enum tr2_sysenv_variable);
const char *tr2_sysenv_display_name(enum tr2_sysenv_variable var);
void tr2_sysenv_release(void);

#endif /* TR2_SYSENV_H */
