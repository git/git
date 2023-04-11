#include "cache.h"
#include "config.h"
#include "column.h"
#include "string-list.h"
#include "pager.h"
#include "parse-options.h"
#include "run-command.h"
#include "utf8.h"

#define XY2LINEAR(d, x, y) (COL_LAYOUT((d)->colopts) == COL_COLUMN ? \
			    (x) * (d)->rows + (y) : \
			    (y) * (d)->cols + (x))

struct column_data {
	const struct string_list *list;
	unsigned int colopts;
	struct column_options opts;

	int rows, cols;
	int *len;		/* cell length */
	int *width;	      /* index to the longest row in column */
};

/* return length of 's' in letters, ANSI escapes stripped */
static int item_length(const char *s)
{
	return utf8_strnwidth(s, strlen(s), 1);
}

/*
 * Calculate cell width, rows and cols for a table of equal cells, given
 * table width and how many spaces between cells.
 */
static void layout(struct column_data *data, int *width)
{
	int i;

	*width = 0;
	for (i = 0; i < data->list->nr; i++)
		if (*width < data->len[i])
			*width = data->len[i];

	*width += data->opts.padding;

	data->cols = (data->opts.width - strlen(data->opts.indent)) / *width;
	if (data->cols == 0)
		data->cols = 1;

	data->rows = DIV_ROUND_UP(data->list->nr, data->cols);
}

static void compute_column_width(struct column_data *data)
{
	int i, x, y;
	for (x = 0; x < data->cols; x++) {
		data->width[x] = XY2LINEAR(data, x, 0);
		for (y = 0; y < data->rows; y++) {
			i = XY2LINEAR(data, x, y);
			if (i < data->list->nr &&
			    data->len[data->width[x]] < data->len[i])
				data->width[x] = i;
		}
	}
}

/*
 * Shrink all columns by shortening them one row each time (and adding
 * more columns along the way). Hopefully the longest cell will be
 * moved to the next column, column is shrunk so we have more space
 * for new columns. The process ends when the whole thing no longer
 * fits in data->total_width.
 */
static void shrink_columns(struct column_data *data)
{
	REALLOC_ARRAY(data->width, data->cols);
	while (data->rows > 1) {
		int x, total_width, cols, rows;
		rows = data->rows;
		cols = data->cols;

		data->rows--;
		data->cols = DIV_ROUND_UP(data->list->nr, data->rows);
		if (data->cols != cols)
			REALLOC_ARRAY(data->width, data->cols);
		compute_column_width(data);

		total_width = strlen(data->opts.indent);
		for (x = 0; x < data->cols; x++) {
			total_width += data->len[data->width[x]];
			total_width += data->opts.padding;
		}
		if (total_width > data->opts.width) {
			data->rows = rows;
			data->cols = cols;
			break;
		}
	}
	compute_column_width(data);
}

/* Display without layout when not enabled */
static void display_plain(const struct string_list *list,
			  const char *indent, const char *nl)
{
	int i;

	for (i = 0; i < list->nr; i++)
		printf("%s%s%s", indent, list->items[i].string, nl);
}

/* Print a cell to stdout with all necessary leading/trailing space */
static int display_cell(struct column_data *data, int initial_width,
			const char *empty_cell, int x, int y)
{
	int i, len, newline;

	i = XY2LINEAR(data, x, y);
	if (i >= data->list->nr)
		return -1;

	len = data->len[i];
	if (data->width && data->len[data->width[x]] < initial_width) {
		/*
		 * empty_cell has initial_width chars, if real column
		 * is narrower, increase len a bit so we fill less
		 * space.
		 */
		len += initial_width - data->len[data->width[x]];
		len -= data->opts.padding;
	}

	if (COL_LAYOUT(data->colopts) == COL_COLUMN)
		newline = i + data->rows >= data->list->nr;
	else
		newline = x == data->cols - 1 || i == data->list->nr - 1;

	printf("%s%s%s",
	       x == 0 ? data->opts.indent : "",
	       data->list->items[i].string,
	       newline ? data->opts.nl : empty_cell + len);
	return 0;
}

/* Display COL_COLUMN or COL_ROW */
static void display_table(const struct string_list *list,
			  unsigned int colopts,
			  const struct column_options *opts)
{
	struct column_data data;
	int x, y, i, initial_width;
	char *empty_cell;

	memset(&data, 0, sizeof(data));
	data.list = list;
	data.colopts = colopts;
	data.opts = *opts;

	ALLOC_ARRAY(data.len, list->nr);
	for (i = 0; i < list->nr; i++)
		data.len[i] = item_length(list->items[i].string);

	layout(&data, &initial_width);

	if (colopts & COL_DENSE)
		shrink_columns(&data);

	empty_cell = xmallocz(initial_width);
	memset(empty_cell, ' ', initial_width);
	for (y = 0; y < data.rows; y++) {
		for (x = 0; x < data.cols; x++)
			if (display_cell(&data, initial_width, empty_cell, x, y))
				break;
	}

	free(data.len);
	free(data.width);
	free(empty_cell);
}

void print_columns(const struct string_list *list, unsigned int colopts,
		   const struct column_options *opts)
{
	struct column_options nopts;

	if (!list->nr)
		return;
	assert((colopts & COL_ENABLE_MASK) != COL_AUTO);

	memset(&nopts, 0, sizeof(nopts));
	nopts.indent = opts && opts->indent ? opts->indent : "";
	nopts.nl = opts && opts->nl ? opts->nl : "\n";
	nopts.padding = opts ? opts->padding : 1;
	nopts.width = opts && opts->width ? opts->width : term_columns() - 1;
	if (!column_active(colopts)) {
		display_plain(list, "", "\n");
		return;
	}
	switch (COL_LAYOUT(colopts)) {
	case COL_PLAIN:
		display_plain(list, nopts.indent, nopts.nl);
		break;
	case COL_ROW:
	case COL_COLUMN:
		display_table(list, colopts, &nopts);
		break;
	default:
		BUG("invalid layout mode %d", COL_LAYOUT(colopts));
	}
}

int finalize_colopts(unsigned int *colopts, int stdout_is_tty)
{
	if ((*colopts & COL_ENABLE_MASK) == COL_AUTO) {
		if (stdout_is_tty < 0)
			stdout_is_tty = isatty(1);
		*colopts &= ~COL_ENABLE_MASK;
		if (stdout_is_tty || pager_in_use())
			*colopts |= COL_ENABLED;
	}
	return 0;
}

struct colopt {
	const char *name;
	unsigned int value;
	unsigned int mask;
};

#define LAYOUT_SET 1
#define ENABLE_SET 2

static int parse_option(const char *arg, int len, unsigned int *colopts,
			int *group_set)
{
	struct colopt opts[] = {
		{ "always", COL_ENABLED,  COL_ENABLE_MASK },
		{ "never",  COL_DISABLED, COL_ENABLE_MASK },
		{ "auto",   COL_AUTO,     COL_ENABLE_MASK },
		{ "plain",  COL_PLAIN,    COL_LAYOUT_MASK },
		{ "column", COL_COLUMN,   COL_LAYOUT_MASK },
		{ "row",    COL_ROW,      COL_LAYOUT_MASK },
		{ "dense",  COL_DENSE,    0 },
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(opts); i++) {
		int set = 1, arg_len = len, name_len;
		const char *arg_str = arg;

		if (!opts[i].mask) {
			if (arg_len > 2 && !strncmp(arg_str, "no", 2)) {
				arg_str += 2;
				arg_len -= 2;
				set = 0;
			}
		}

		name_len = strlen(opts[i].name);
		if (arg_len != name_len ||
		    strncmp(arg_str, opts[i].name, name_len))
			continue;

		switch (opts[i].mask) {
		case COL_ENABLE_MASK:
			*group_set |= ENABLE_SET;
			break;
		case COL_LAYOUT_MASK:
			*group_set |= LAYOUT_SET;
			break;
		}

		if (opts[i].mask)
			*colopts = (*colopts & ~opts[i].mask) | opts[i].value;
		else {
			if (set)
				*colopts |= opts[i].value;
			else
				*colopts &= ~opts[i].value;
		}
		return 0;
	}

	return error("unsupported option '%s'", arg);
}

static int parse_config(unsigned int *colopts, const char *value)
{
	const char *sep = " ,";
	int group_set = 0;

	while (*value) {
		int len = strcspn(value, sep);
		if (len) {
			if (parse_option(value, len, colopts, &group_set))
				return -1;

			value += len;
		}
		value += strspn(value, sep);
	}
	/*
	 * If none of "always", "never", and "auto" is specified, then setting
	 * layout implies "always".
	 *
	 * Current value in COL_ENABLE_MASK is disregarded. This means if
	 * you set column.ui = auto and pass --column=row, then "auto"
	 * will become "always".
	 */
	if ((group_set & LAYOUT_SET) && !(group_set & ENABLE_SET))
		*colopts = (*colopts & ~COL_ENABLE_MASK) | COL_ENABLED;
	return 0;
}

static int column_config(const char *var, const char *value,
			 const char *key, unsigned int *colopts)
{
	if (!value)
		return config_error_nonbool(var);
	if (parse_config(colopts, value))
		return error("invalid column.%s mode %s", key, value);
	return 0;
}

int git_column_config(const char *var, const char *value,
		      const char *command, unsigned int *colopts)
{
	const char *it;

	if (!skip_prefix(var, "column.", &it))
		return 0;

	if (!strcmp(it, "ui"))
		return column_config(var, value, "ui", colopts);

	if (command && !strcmp(it, command))
		return column_config(var, value, it, colopts);

	return 0;
}

int parseopt_column_callback(const struct option *opt,
			     const char *arg, int unset)
{
	unsigned int *colopts = opt->value;
	*colopts |= COL_PARSEOPT;
	*colopts &= ~COL_ENABLE_MASK;
	if (unset)		/* --no-column == never */
		return 0;
	/* --column == always unless "arg" states otherwise */
	*colopts |= COL_ENABLED;
	if (arg)
		return parse_config(colopts, arg);

	return 0;
}

static int fd_out = -1;
static struct child_process column_process = CHILD_PROCESS_INIT;

int run_column_filter(int colopts, const struct column_options *opts)
{
	struct strvec *argv;

	if (fd_out != -1)
		return -1;

	child_process_init(&column_process);
	argv = &column_process.args;

	strvec_push(argv, "column");
	strvec_pushf(argv, "--raw-mode=%d", colopts);
	if (opts && opts->width)
		strvec_pushf(argv, "--width=%d", opts->width);
	if (opts && opts->indent)
		strvec_pushf(argv, "--indent=%s", opts->indent);
	if (opts && opts->padding)
		strvec_pushf(argv, "--padding=%d", opts->padding);

	fflush(stdout);
	column_process.in = -1;
	column_process.out = dup(1);
	column_process.git_cmd = 1;

	if (start_command(&column_process))
		return -2;

	fd_out = dup(1);
	close(1);
	dup2(column_process.in, 1);
	close(column_process.in);
	return 0;
}

int stop_column_filter(void)
{
	if (fd_out == -1)
		return -1;

	fflush(stdout);
	close(1);
	finish_command(&column_process);
	dup2(fd_out, 1);
	close(fd_out);
	fd_out = -1;
	return 0;
}
