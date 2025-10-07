#include "basics.h"
#include "reftable-fsck.h"
#include "reftable-table.h"
#include "stack.h"

static bool table_has_valid_name(const char *name)
{
	const char *ptr = name;
	char *endptr;

	/* strtoull doesn't set errno on success */
	errno = 0;

	strtoull(ptr, &endptr, 16);
	if (errno)
		return false;
	ptr = endptr;

	if (*ptr != '-')
		return false;
	ptr++;

	strtoull(ptr, &endptr, 16);
	if (errno)
		return false;
	ptr = endptr;

	if (*ptr != '-')
		return false;
	ptr++;

	strtoul(ptr, &endptr, 16);
	if (errno)
		return false;
	ptr = endptr;

	if (strcmp(ptr, ".ref") && strcmp(ptr, ".log"))
		return false;

	return true;
}

typedef int (*table_check_fn)(struct reftable_table *table,
			      reftable_fsck_report_fn report_fn,
			      void *cb_data);

static int table_check_name(struct reftable_table *table,
			    reftable_fsck_report_fn report_fn,
			    void *cb_data)
{
	if (!table_has_valid_name(table->name)) {
		struct reftable_fsck_info info;

		info.error = REFTABLE_FSCK_ERROR_TABLE_NAME;
		info.msg = "invalid reftable table name";
		info.path = table->name;

		return report_fn(&info, cb_data);
	}

	return 0;
}

static int table_checks(struct reftable_table *table,
			reftable_fsck_report_fn report_fn,
			reftable_fsck_verbose_fn verbose_fn UNUSED,
			void *cb_data)
{
	table_check_fn table_check_fns[] = {
		table_check_name,
		NULL,
	};
	int err = 0;

	for (size_t i = 0; table_check_fns[i]; i++)
		err |= table_check_fns[i](table, report_fn, cb_data);

	return err;
}

int reftable_fsck_check(struct reftable_stack *stack,
			reftable_fsck_report_fn report_fn,
			reftable_fsck_verbose_fn verbose_fn,
			void *cb_data)
{
	struct reftable_buf msg = REFTABLE_BUF_INIT;
	int err = 0;

	for (size_t i = 0; i < stack->tables_len; i++) {
		reftable_buf_reset(&msg);
		reftable_buf_addstr(&msg, "Checking table: ");
		reftable_buf_addstr(&msg, stack->tables[i]->name);
		verbose_fn(msg.buf, cb_data);

		err |= table_checks(stack->tables[i], report_fn, verbose_fn, cb_data);
	}

	reftable_buf_release(&msg);
	return err;
}
