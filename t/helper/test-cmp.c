#include "test-tool.h"
#include "git-compat-util.h"
#include "strbuf.h"
#include "gettext.h"
#include "parse-options.h"
#include "run-command.h"

#ifdef WIN32
#define NO_SUCH_DIR "\\\\.\\GLOBALROOT\\invalid"
#else
#define NO_SUCH_DIR "/dev/null"
#endif

static int run_diff(const char *path1, const char *path2)
{
	const char *argv[] = {
		"diff", "--no-index", NULL, NULL, NULL
	};
	const char *env[] = {
		"GIT_PAGER=cat",
		"GIT_DIR=" NO_SUCH_DIR,
		"HOME=" NO_SUCH_DIR,
		NULL
	};

	argv[2] = path1;
	argv[3] = path2;
	return run_command_v_opt_cd_env(argv,
					RUN_COMMAND_NO_STDIN | RUN_GIT_CMD,
					NULL, env);
}

int cmd__cmp(int argc, const char **argv)
{
	FILE *f0, *f1;
	struct strbuf b0 = STRBUF_INIT, b1 = STRBUF_INIT;

	if (argc != 3)
		die("Require exactly 2 arguments, got %d", argc);

	if (!(f0 = !strcmp(argv[1], "-") ? stdin : fopen(argv[1], "r")))
		return error_errno("could not open '%s'", argv[1]);
	if (!(f1 = !strcmp(argv[2], "-") ? stdin : fopen(argv[2], "r"))) {
		fclose(f0);
		return error_errno("could not open '%s'", argv[2]);
	}

	for (;;) {
		int r0 = strbuf_getline(&b0, f0);
		int r1 = strbuf_getline(&b1, f1);

		if (r0 == EOF) {
			fclose(f0);
			fclose(f1);
			strbuf_release(&b0);
			strbuf_release(&b1);
			if (r1 == EOF)
				return 0;
cmp_failed:
			if (!run_diff(argv[1], argv[2]))
				die("Huh? 'diff --no-index %s %s' succeeded",
				    argv[1], argv[2]);
			return 1;
		}
		if (r1 == EOF || strbuf_cmp(&b0, &b1)) {
			fclose(f0);
			fclose(f1);
			strbuf_release(&b0);
			strbuf_release(&b1);
			goto cmp_failed;
		}
	}
}
