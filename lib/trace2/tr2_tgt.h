#ifndef TR2_TGT_H
#define TR2_TGT_H

struct child_process;
struct repository;
struct json_writer;
struct tr2_timer_metadata;
struct tr2_timer;
struct tr2_counter_metadata;
struct tr2_counter;

#define NS_TO_SEC(ns) ((double)(ns) / 1.0e9)

/*
 * Function prototypes for a TRACE2 "target" vtable.
 */

typedef int(tr2_tgt_init_t)(void);
typedef void(tr2_tgt_term_t)(void);

typedef void(tr2_tgt_evt_version_fl_t)(const char *file, int line);

typedef void(tr2_tgt_evt_start_fl_t)(const char *file, int line,
				     uint64_t us_elapsed_absolute,
				     const char **argv);
typedef void(tr2_tgt_evt_exit_fl_t)(const char *file, int line,
				    uint64_t us_elapsed_absolute, int code);
typedef void(tr2_tgt_evt_signal_t)(uint64_t us_elapsed_absolute, int signo);
typedef void(tr2_tgt_evt_atexit_t)(uint64_t us_elapsed_absolute, int code);

typedef void(tr2_tgt_evt_error_va_fl_t)(const char *file, int line,
					const char *fmt, va_list ap);

typedef void(tr2_tgt_evt_command_path_fl_t)(const char *file, int line,
					    const char *command_path);
typedef void(tr2_tgt_evt_command_ancestry_fl_t)(const char *file, int line,
						const char **parent_names);
typedef void(tr2_tgt_evt_command_name_fl_t)(const char *file, int line,
					    const char *name,
					    const char *hierarchy);
typedef void(tr2_tgt_evt_command_mode_fl_t)(const char *file, int line,
					    const char *mode);

typedef void(tr2_tgt_evt_alias_fl_t)(const char *file, int line,
				     const char *alias, const char **argv);

typedef void(tr2_tgt_evt_child_start_fl_t)(const char *file, int line,
					   uint64_t us_elapsed_absolute,
					   const struct child_process *cmd);
typedef void(tr2_tgt_evt_child_exit_fl_t)(const char *file, int line,
					  uint64_t us_elapsed_absolute, int cid,
					  int pid, int code,
					  uint64_t us_elapsed_child);
typedef void(tr2_tgt_evt_child_ready_fl_t)(const char *file, int line,
					   uint64_t us_elapsed_absolute,
					   int cid, int pid, const char *ready,
					   uint64_t us_elapsed_child);

typedef void(tr2_tgt_evt_thread_start_fl_t)(const char *file, int line,
					    uint64_t us_elapsed_absolute);
typedef void(tr2_tgt_evt_thread_exit_fl_t)(const char *file, int line,
					   uint64_t us_elapsed_absolute,
					   uint64_t us_elapsed_thread);

typedef void(tr2_tgt_evt_exec_fl_t)(const char *file, int line,
				    uint64_t us_elapsed_absolute, int exec_id,
				    const char *exe, const char **argv);
typedef void(tr2_tgt_evt_exec_result_fl_t)(const char *file, int line,
					   uint64_t us_elapsed_absolute,
					   int exec_id, int code);

struct key_value_info;
typedef void(tr2_tgt_evt_param_fl_t)(const char *file, int line,
				     const char *param, const char *value,
				     const struct key_value_info *kvi);

typedef void(tr2_tgt_evt_repo_fl_t)(const char *file, int line,
				    const struct repository *repo);

typedef void(tr2_tgt_evt_region_enter_printf_va_fl_t)(
	const char *file, int line, uint64_t us_elapsed_absolute,
	const char *category, const char *label, const struct repository *repo,
	const char *fmt, va_list ap);
typedef void(tr2_tgt_evt_region_leave_printf_va_fl_t)(
	const char *file, int line, uint64_t us_elapsed_absolute,
	uint64_t us_elapsed_region, const char *category, const char *label,
	const struct repository *repo, const char *fmt, va_list ap);

typedef void(tr2_tgt_evt_data_fl_t)(const char *file, int line,
				    uint64_t us_elapsed_absolute,
				    uint64_t us_elapsed_region,
				    const char *category,
				    const struct repository *repo,
				    const char *key, const char *value);
typedef void(tr2_tgt_evt_data_json_fl_t)(const char *file, int line,
					 uint64_t us_elapsed_absolute,
					 uint64_t us_elapsed_region,
					 const char *category,
					 const struct repository *repo,
					 const char *key,
					 const struct json_writer *value);

typedef void(tr2_tgt_evt_printf_va_fl_t)(const char *file, int line,
					 uint64_t us_elapsed_absolute,
					 const char *fmt, va_list ap);

typedef void(tr2_tgt_evt_timer_t)(const struct tr2_timer_metadata *meta,
				  const struct tr2_timer *timer,
				  int is_final_data);

typedef void(tr2_tgt_evt_counter_t)(const struct tr2_counter_metadata *meta,
				    const struct tr2_counter *counter,
				    int is_final_data);

/*
 * "vtable" for a TRACE2 target.  Use NULL if a target does not want
 * to emit that message.
 */
/* clang-format off */
struct tr2_tgt {
	struct tr2_dst                          *pdst;

	tr2_tgt_init_t                          *pfn_init;
	tr2_tgt_term_t                          *pfn_term;

	tr2_tgt_evt_version_fl_t                *pfn_version_fl;
	tr2_tgt_evt_start_fl_t                  *pfn_start_fl;
	tr2_tgt_evt_exit_fl_t                   *pfn_exit_fl;
	tr2_tgt_evt_signal_t                    *pfn_signal;
	tr2_tgt_evt_atexit_t                    *pfn_atexit;
	tr2_tgt_evt_error_va_fl_t               *pfn_error_va_fl;
	tr2_tgt_evt_command_path_fl_t           *pfn_command_path_fl;
	tr2_tgt_evt_command_ancestry_fl_t	*pfn_command_ancestry_fl;
	tr2_tgt_evt_command_name_fl_t           *pfn_command_name_fl;
	tr2_tgt_evt_command_mode_fl_t           *pfn_command_mode_fl;
	tr2_tgt_evt_alias_fl_t                  *pfn_alias_fl;
	tr2_tgt_evt_child_start_fl_t            *pfn_child_start_fl;
	tr2_tgt_evt_child_exit_fl_t             *pfn_child_exit_fl;
	tr2_tgt_evt_child_ready_fl_t            *pfn_child_ready_fl;
	tr2_tgt_evt_thread_start_fl_t           *pfn_thread_start_fl;
	tr2_tgt_evt_thread_exit_fl_t            *pfn_thread_exit_fl;
	tr2_tgt_evt_exec_fl_t                   *pfn_exec_fl;
	tr2_tgt_evt_exec_result_fl_t            *pfn_exec_result_fl;
	tr2_tgt_evt_param_fl_t                  *pfn_param_fl;
	tr2_tgt_evt_repo_fl_t                   *pfn_repo_fl;
	tr2_tgt_evt_region_enter_printf_va_fl_t *pfn_region_enter_printf_va_fl;
	tr2_tgt_evt_region_leave_printf_va_fl_t *pfn_region_leave_printf_va_fl;
	tr2_tgt_evt_data_fl_t                   *pfn_data_fl;
	tr2_tgt_evt_data_json_fl_t              *pfn_data_json_fl;
	tr2_tgt_evt_printf_va_fl_t              *pfn_printf_va_fl;
	tr2_tgt_evt_timer_t                     *pfn_timer;
	tr2_tgt_evt_counter_t                   *pfn_counter;
};
/* clang-format on */

extern struct tr2_tgt tr2_tgt_event;
extern struct tr2_tgt tr2_tgt_normal;
extern struct tr2_tgt tr2_tgt_perf;

#endif /* TR2_TGT_H */
