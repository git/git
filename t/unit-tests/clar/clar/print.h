/* clap: clar protocol, the traditional clar output format */

static void clar_print_clap_init(int test_count, int suite_count)
{
	(void)test_count;

	if (_clar.verbosity < 0)
		return;

	printf("Loaded %d suites:\n", (int)suite_count);
	printf("Started (test status codes: OK='.' FAILURE='F' SKIPPED='S')\n");
}

static void clar_print_clap_shutdown(int test_count, int suite_count, int error_count)
{
	(void)test_count;
	(void)suite_count;
	(void)error_count;

	if (_clar.verbosity >= 0)
		printf("\n\n");
	clar_report_all();
}


static void clar_print_indented(const char *str, int indent)
{
	const char *bol, *eol;

	for (bol = str; *bol; bol = eol) {
		eol = strchr(bol, '\n');
		if (eol)
			eol++;
		else
			eol = bol + strlen(bol);
		printf("%*s%.*s", indent, "", (int)(eol - bol), bol);
	}
	putc('\n', stdout);
}

static void clar_print_clap_error(int num, const struct clar_report *report, const struct clar_error *error)
{
	printf("  %d) Failure:\n", num);

	printf("%s::%s [%s:%"PRIuMAX"]\n",
		report->suite,
		report->test,
		error->file,
		error->line_number);

	clar_print_indented(error->error_msg, 2);

	if (error->description != NULL)
		clar_print_indented(error->description, 2);

	printf("\n");
	fflush(stdout);
}

static void clar_print_clap_ontest(const char *suite_name, const char *test_name, int test_number, enum cl_test_status status)
{
	(void)test_name;
	(void)test_number;

	if (_clar.verbosity < 0)
		return;

	if (_clar.verbosity > 1) {
		printf("%s::%s: ", suite_name, test_name);

		switch (status) {
		case CL_TEST_OK: printf("ok\n"); break;
		case CL_TEST_FAILURE: printf("fail\n"); break;
		case CL_TEST_SKIP: printf("skipped\n"); break;
		case CL_TEST_NOTRUN: printf("notrun\n"); break;
		}
	} else {
		switch (status) {
		case CL_TEST_OK: printf("."); break;
		case CL_TEST_FAILURE: printf("F"); break;
		case CL_TEST_SKIP: printf("S"); break;
		case CL_TEST_NOTRUN: printf("N"); break;
		}

		fflush(stdout);
	}
}

static void clar_print_clap_onsuite(const char *suite_name, int suite_index)
{
	if (_clar.verbosity < 0)
		return;
	if (_clar.verbosity == 1)
		printf("\n%s", suite_name);

	(void)suite_index;
}

static void clar_print_clap_onabort(const char *fmt, va_list arg)
{
	vfprintf(stderr, fmt, arg);
}

/* tap: test anywhere protocol format */

static void clar_print_tap_init(int test_count, int suite_count)
{
	(void)test_count;
	(void)suite_count;
	printf("TAP version 13\n");
}

static void clar_print_tap_shutdown(int test_count, int suite_count, int error_count)
{
	(void)suite_count;
	(void)error_count;

	printf("1..%d\n", test_count);
}

static void clar_print_tap_error(int num, const struct clar_report *report, const struct clar_error *error)
{
	(void)num;
	(void)report;
	(void)error;
}

static void print_escaped(const char *str)
{
	char *c;

	while ((c = strchr(str, '\'')) != NULL) {
		printf("%.*s", (int)(c - str), str);
		printf("''");
		str = c + 1;
	}

	printf("%s", str);
}

static void clar_print_tap_ontest(const char *suite_name, const char *test_name, int test_number, enum cl_test_status status)
{
	const struct clar_error *error = _clar.last_report->errors;

	(void)test_name;
	(void)test_number;

	switch(status) {
	case CL_TEST_OK:
		printf("ok %d - %s::%s\n", test_number, suite_name, test_name);
		break;
	case CL_TEST_FAILURE:
		printf("not ok %d - %s::%s\n", test_number, suite_name, test_name);

		if (_clar.verbosity >= 0) {
			printf("    ---\n");
			printf("    reason: |\n");
			clar_print_indented(error->error_msg, 6);

			if (error->description)
				clar_print_indented(error->description, 6);

			printf("    at:\n");
			printf("      file: '"); print_escaped(error->file); printf("'\n");
			printf("      line: %" PRIuMAX "\n", error->line_number);
			printf("      function: '%s'\n", error->function);
			printf("    ---\n");
		}

		break;
	case CL_TEST_SKIP:
	case CL_TEST_NOTRUN:
		printf("ok %d - # SKIP %s::%s\n", test_number, suite_name, test_name);
		break;
	}

	fflush(stdout);
}

static void clar_print_tap_onsuite(const char *suite_name, int suite_index)
{
	if (_clar.verbosity < 0)
		return;
	printf("# start of suite %d: %s\n", suite_index, suite_name);
}

static void clar_print_tap_onabort(const char *fmt, va_list arg)
{
	printf("Bail out! ");
	vprintf(fmt, arg);
	fflush(stdout);
}

/* indirection between protocol output selection */

#define PRINT(FN, ...) do { \
		switch (_clar.output_format) { \
			case CL_OUTPUT_CLAP: \
				clar_print_clap_##FN (__VA_ARGS__); \
				break; \
			case CL_OUTPUT_TAP: \
				clar_print_tap_##FN (__VA_ARGS__); \
				break; \
			default: \
				abort(); \
		} \
	} while (0)

static void clar_print_init(int test_count, int suite_count)
{
	PRINT(init, test_count, suite_count);
}

static void clar_print_shutdown(int test_count, int suite_count, int error_count)
{
	PRINT(shutdown, test_count, suite_count, error_count);
}

static void clar_print_error(int num, const struct clar_report *report, const struct clar_error *error)
{
	PRINT(error, num, report, error);
}

static void clar_print_ontest(const char *suite_name, const char *test_name, int test_number, enum cl_test_status status)
{
	PRINT(ontest, suite_name, test_name, test_number, status);
}

static void clar_print_onsuite(const char *suite_name, int suite_index)
{
	PRINT(onsuite, suite_name, suite_index);
}

static void clar_print_onabortv(const char *msg, va_list argp)
{
	PRINT(onabort, msg, argp);
}

static void clar_print_onabort(const char *msg, ...)
{
	va_list argp;
	va_start(argp, msg);
	clar_print_onabortv(msg, argp);
	va_end(argp);
}
