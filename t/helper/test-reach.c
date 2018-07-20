#include "test-tool.h"
#include "cache.h"
#include "commit.h"
#include "commit-reach.h"
#include "config.h"
#include "parse-options.h"
#include "tag.h"

int cmd__reach(int ac, const char **av)
{
	struct object_id oid_A, oid_B;
	struct commit *A, *B;
	struct strbuf buf = STRBUF_INIT;
	struct repository *r = the_repository;

	setup_git_directory();

	if (ac < 2)
		exit(1);

	A = B = NULL;

	while (strbuf_getline(&buf, stdin) != EOF) {
		struct object_id oid;
		struct object *o;
		struct commit *c;
		if (buf.len < 3)
			continue;

		if (get_oid_committish(buf.buf + 2, &oid))
			die("failed to resolve %s", buf.buf + 2);

		o = parse_object(r, &oid);
		o = deref_tag_noverify(o);

		if (!o)
			die("failed to load commit for input %s resulting in oid %s\n",
			    buf.buf, oid_to_hex(&oid));

		c = object_as_type(r, o, OBJ_COMMIT, 0);

		if (!c)
			die("failed to load commit for input %s resulting in oid %s\n",
			    buf.buf, oid_to_hex(&oid));

		switch (buf.buf[0]) {
			case 'A':
				oidcpy(&oid_A, &oid);
				A = c;
				break;

			case 'B':
				oidcpy(&oid_B, &oid);
				B = c;
				break;

			default:
				die("unexpected start of line: %c", buf.buf[0]);
		}
	}
	strbuf_release(&buf);

	if (!strcmp(av[1], "ref_newer"))
		printf("%s(A,B):%d\n", av[1], ref_newer(&oid_A, &oid_B));
	else if (!strcmp(av[1], "in_merge_bases"))
		printf("%s(A,B):%d\n", av[1], in_merge_bases(A, B));

	exit(0);
}
