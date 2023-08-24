#include "test-lib.h"
#include "progress.c"


static void t_simple_progress()
{
	int total = 4;
	struct progress *progress = NULL;
	int i;
	progress = start_progress("Working hard", total);
	for (i = 1; i <= total; i++) {
		display_progress(progress, i);
		check_uint(i, ==, progress->last_value);
		check_str(progress->title, "Working hard");
		check_int(progress->last_percent, ==, i * 100 / total);
	}
	return;
}

static void t_simple_progress_percent_text()
{
	int total = 4;
	struct progress *progress = NULL;
	int i;
	char *expected[] = {
		"  0% (0/4)",
		" 25% (1/4)",
		" 50% (2/4)",
		" 75% (3/4)",
		"100% (4/4)"
		};
	char *instructions[] = {
		"progress",
		"progress",
		"progress",
		"progress",
		"progress"
	};
	int value[] = {
		0,
		1,
		2,
		3,
		4
	};
	progress = start_progress("Working hard", total);
	for (i = 0; i < 5; i++) {
		if(strcmp(instructions[i], "progress")==0){
			display_progress(progress, value[i]);
			check_str(progress->title, "Working hard");
			check_str(progress->counters_sb.buf, expected[i]);
			check_uint(i * (100 / total), ==, progress->last_percent);
		}
	}
	return;
}

static void t_progress_display_breaks_long_lines_1()
{
	int total = 100000;
	struct progress *progress = NULL;
	int i;
	char *expected[4] = {
		"  0% (100/100000)",
		"  1% (1000/100000)",
		" 10% (10000/100000)",
		"100% (100000/100000)"
	};
	char *instructions[] = {
		"progress",
		"progress",
		"progress",
		"progress"
	};
	int value[] = {
		100,
		1000,
		10000,
		100000
	};
	progress = start_progress(
		"Working hard.......2.........3.........4.........5.........6",
		total);
	for (i = 0; i < 4; i++) {
		if(strcmp(instructions[i], "progress")==0){
			display_progress(progress, value[i]);
		}
		check_str(progress->title, "Working hard.......2.........3.........4.........5.........6");
		check_str(progress->counters_sb.buf, expected[i]);
	}
	return;
}

static void t_progress_display_breaks_long_lines_2()
{
	int total = 100000;
	struct progress *progress = NULL;
	int i;
	char *expected[] = {
		"",
		"  0% (1/100000)",
		"",
		"  0% (2/100000)",
		" 10% (10000/100000)",
		"100% (100000/100000)"
	};
	char *instructions[] = {
		"update",
		"progress",
		"update",
		"progress",
		"progress",
		"progress"
	};
	int value[] = {
		-1,
		1,
		-1,
		2,
		10000,
		100000
	};
	progress = start_progress(
		"Working hard.......2.........3.........4.........5.........6",
		total);
	for (i = 0; i < 5; i++) {
		if(strcmp(instructions[i], "progress")==0){
			display_progress(progress, value[i]);
			check_str(progress->title, "Working hard.......2.........3.........4.........5.........6");
			check_str(progress->counters_sb.buf, expected[i]);
		}else if(strcmp(instructions[i], "update")==0){
			progress_test_force_update();
		}
	}
	return;
}

static void t_progress_display_breaks_long_lines_3()
{
	int total = 100000;
	struct progress *progress = NULL;
	int i;
	char *expected[4] = {
		" 25% (25000/100000)",
		" 50% (50000/100000)",
		" 75% (75000/100000)",
		"100% (100000/100000)"
	};
	char *instructions[] = {
		"progress",
		"progress",
		"progress",
		"progress"
	};
	int value[] = {
		25000,
		50000,
		75000,
		100000
	};
	progress = start_progress(
		"Working hard.......2.........3.........4.........5.........6",
		total);
	for (i = 0; i < 4; i++) {
		if(strcmp(instructions[i], "progress")==0){
			display_progress(progress, value[i]);
			check_str(progress->title, "Working hard.......2.........3.........4.........5.........6");
			check_str(progress->counters_sb.buf, expected[i]);
		}else if(strcmp(instructions[i], "update")==0){
			progress_test_force_update();
		}
	}
	return;
}


static void t_progress_shortens_crazy_caller()
{
	int total = 1000;
	struct progress *progress = NULL;
	int i;
	char *expected[4] = {
		" 10% (100/1000)",
		" 20% (200/1000)",
		"  0% (1/1000)",
		"100% (1000/1000)"
	};
	char *instructions[] = {
		"progress",
		"progress",
		"progress",
		"progress"
	};
	int value[] = {
		100,
		200,
		1,
		1000
	};
	progress = start_progress(
		"Working hard.......2.........3.........4.........5.........6",
		total);
	for (i = 0; i < 4; i++) {
		if(strcmp(instructions[i], "progress")==0){
			display_progress(progress, value[i]);
			check_str(progress->title, "Working hard.......2.........3.........4.........5.........6");
			check_str(progress->counters_sb.buf, expected[i]);
		}else if(strcmp(instructions[i], "update")==0){
			progress_test_force_update();
		}
	}
	return;
}

int cmd_main(int argc, const char **argv)
{
	TEST(t_simple_progress(), "Simple progress upto 3 units");
	TEST(t_simple_progress_percent_text(),
	     "Simple progress with percent output");
	TEST(t_progress_display_breaks_long_lines_1(),
	     "progress display breaks long lines #1");
	TEST(t_progress_display_breaks_long_lines_2(),
	     "progress display breaks long lines #2");
	TEST(t_progress_display_breaks_long_lines_3(),
	     "progress display breaks long lines #3");
	TEST(t_progress_shortens_crazy_caller(),
	     "progress shortens - crazy caller");
	return test_done();
}
