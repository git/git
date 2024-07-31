
#include <stdio.h>
#include <time.h>

static int clar_summary_close_tag(
    struct clar_summary *summary, const char *tag, int indent)
{
	const char *indt;

	if (indent == 0) indt = "";
	else if (indent == 1) indt = "\t";
	else indt = "\t\t";

	return fprintf(summary->fp, "%s</%s>\n", indt, tag);
}

static int clar_summary_testsuites(struct clar_summary *summary)
{
	return fprintf(summary->fp, "<testsuites>\n");
}

static int clar_summary_testsuite(struct clar_summary *summary,
    int idn, const char *name, time_t timestamp,
    int test_count, int fail_count, int error_count)
{
	struct tm *tm = localtime(&timestamp);
	char iso_dt[20];

	if (strftime(iso_dt, sizeof(iso_dt), "%Y-%m-%dT%H:%M:%S", tm) == 0)
		return -1;

	return fprintf(summary->fp, "\t<testsuite"
		       " id=\"%d\""
		       " name=\"%s\""
		       " hostname=\"localhost\""
		       " timestamp=\"%s\""
		       " tests=\"%d\""
		       " failures=\"%d\""
		       " errors=\"%d\">\n",
		       idn, name, iso_dt, test_count, fail_count, error_count);
}

static int clar_summary_testcase(struct clar_summary *summary,
    const char *name, const char *classname, double elapsed)
{
	return fprintf(summary->fp,
	    "\t\t<testcase name=\"%s\" classname=\"%s\" time=\"%.2f\">\n",
		name, classname, elapsed);
}

static int clar_summary_failure(struct clar_summary *summary,
    const char *type, const char *message, const char *desc)
{
	return fprintf(summary->fp,
	    "\t\t\t<failure type=\"%s\"><![CDATA[%s\n%s]]></failure>\n",
	    type, message, desc);
}

static int clar_summary_skipped(struct clar_summary *summary)
{
	return fprintf(summary->fp, "\t\t\t<skipped />\n");
}

struct clar_summary *clar_summary_init(const char *filename)
{
	struct clar_summary *summary;
	FILE *fp;

	if ((fp = fopen(filename, "w")) == NULL) {
		perror("fopen");
		return NULL;
	}

	if ((summary = malloc(sizeof(struct clar_summary))) == NULL) {
		perror("malloc");
		fclose(fp);
		return NULL;
	}

	summary->filename = filename;
	summary->fp = fp;

	return summary;
}

int clar_summary_shutdown(struct clar_summary *summary)
{
	struct clar_report *report;
	const char *last_suite = NULL;

	if (clar_summary_testsuites(summary) < 0)
		goto on_error;

	report = _clar.reports;
	while (report != NULL) {
		struct clar_error *error = report->errors;

		if (last_suite == NULL || strcmp(last_suite, report->suite) != 0) {
			if (clar_summary_testsuite(summary, 0, report->suite,
			    report->start, _clar.tests_ran, _clar.total_errors, 0) < 0)
				goto on_error;
		}

		last_suite = report->suite;

		clar_summary_testcase(summary, report->test, report->suite, report->elapsed);

		while (error != NULL) {
			if (clar_summary_failure(summary, "assert",
			    error->error_msg, error->description) < 0)
				goto on_error;

			error = error->next;
		}

		if (report->status == CL_TEST_SKIP)
			clar_summary_skipped(summary);

		if (clar_summary_close_tag(summary, "testcase", 2) < 0)
			goto on_error;

		report = report->next;

		if (!report || strcmp(last_suite, report->suite) != 0) {
			if (clar_summary_close_tag(summary, "testsuite", 1) < 0)
				goto on_error;
		}
	}

	if (clar_summary_close_tag(summary, "testsuites", 0) < 0 ||
	    fclose(summary->fp) != 0)
		goto on_error;

	printf("written summary file to %s\n", summary->filename);

	free(summary);
	return 0;

on_error:
	fclose(summary->fp);
	free(summary);
	return -1;
}
