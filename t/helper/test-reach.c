#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "commit.h"
#include "commit-reach.h"
#include "gettext.h"
#include "hex.h"
#include "object-name.h"
#include "ref-filter.h"
#include "setup.h"
#include "string-list.h"
#include "tag.h"

static void print_sorted_commit_ids(struct commit_list *list)
{
	int i;
	struct string_list s = STRING_LIST_INIT_DUP;

	while (list) {
		string_list_append(&s, oid_to_hex(&list->item->object.oid));
		list = list->next;
	}

	string_list_sort(&s);

	for (i = 0; i < s.nr; i++)
		printf("%s\n", s.items[i].string);

	string_list_clear(&s, 0);
}

int cmd__reach(int ac, const char **av)
{
	struct object_id oid_A, oid_B;
	struct commit *A, *B;
	struct commit_list *X, *Y;
	struct object_array X_obj = OBJECT_ARRAY_INIT;
	struct commit **X_array, **Y_array;
	int X_nr, X_alloc, Y_nr, Y_alloc;
	struct strbuf buf = STRBUF_INIT;
	struct repository *r = the_repository;

	setup_git_directory();

	if (ac < 2)
		exit(1);

	A = B = NULL;
	X = Y = NULL;
	X_nr = Y_nr = 0;
	X_alloc = Y_alloc = 16;
	ALLOC_ARRAY(X_array, X_alloc);
	ALLOC_ARRAY(Y_array, Y_alloc);

	while (strbuf_getline(&buf, stdin) != EOF) {
		struct object_id oid;
		struct object *orig;
		struct object *peeled;
		struct commit *c;
		if (buf.len < 3)
			continue;

		if (repo_get_oid_committish(the_repository, buf.buf + 2, &oid))
			die("failed to resolve %s", buf.buf + 2);

		orig = parse_object(r, &oid);
		peeled = deref_tag_noverify(the_repository, orig);

		if (!peeled)
			die("failed to load commit for input %s resulting in oid %s",
			    buf.buf, oid_to_hex(&oid));

		c = object_as_type(peeled, OBJ_COMMIT, 0);

		if (!c)
			die("failed to load commit for input %s resulting in oid %s",
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

			case 'X':
				commit_list_insert(c, &X);
				ALLOC_GROW(X_array, X_nr + 1, X_alloc);
				X_array[X_nr++] = c;
				add_object_array(orig, NULL, &X_obj);
				break;

			case 'Y':
				commit_list_insert(c, &Y);
				ALLOC_GROW(Y_array, Y_nr + 1, Y_alloc);
				Y_array[Y_nr++] = c;
				break;

			default:
				die("unexpected start of line: %c", buf.buf[0]);
		}
	}
	strbuf_release(&buf);

	if (!strcmp(av[1], "ref_newer"))
		printf("%s(A,B):%d\n", av[1], ref_newer(&oid_A, &oid_B));
	else if (!strcmp(av[1], "in_merge_bases"))
		printf("%s(A,B):%d\n", av[1],
		       repo_in_merge_bases(the_repository, A, B));
	else if (!strcmp(av[1], "in_merge_bases_many"))
		printf("%s(A,X):%d\n", av[1],
		       repo_in_merge_bases_many(the_repository, A, X_nr, X_array, 0));
	else if (!strcmp(av[1], "is_descendant_of"))
		printf("%s(A,X):%d\n", av[1], repo_is_descendant_of(r, A, X));
	else if (!strcmp(av[1], "get_branch_base_for_tip"))
		printf("%s(A,X):%d\n", av[1], get_branch_base_for_tip(r, A, X_array, X_nr));
	else if (!strcmp(av[1], "get_merge_bases_many")) {
		struct commit_list *list = NULL;
		if (repo_get_merge_bases_many(the_repository,
					      A, X_nr,
					      X_array,
					      &list) < 0)
			exit(128);
		printf("%s(A,X):\n", av[1]);
		print_sorted_commit_ids(list);
		free_commit_list(list);
	} else if (!strcmp(av[1], "reduce_heads")) {
		struct commit_list *list = reduce_heads(X);
		printf("%s(X):\n", av[1]);
		print_sorted_commit_ids(list);
		free_commit_list(list);
	} else if (!strcmp(av[1], "can_all_from_reach")) {
		printf("%s(X,Y):%d\n", av[1], can_all_from_reach(X, Y, 1));
	} else if (!strcmp(av[1], "can_all_from_reach_with_flag")) {
		struct commit_list *iter = Y;

		while (iter) {
			iter->item->object.flags |= 2;
			iter = iter->next;
		}

		printf("%s(X,_,_,0,0):%d\n", av[1], can_all_from_reach_with_flag(&X_obj, 2, 4, 0, 0));
	} else if (!strcmp(av[1], "commit_contains")) {
		struct ref_filter filter = REF_FILTER_INIT;
		struct contains_cache cache;
		init_contains_cache(&cache);

		if (ac > 2 && !strcmp(av[2], "--tag"))
			filter.with_commit_tag_algo = 1;
		else
			filter.with_commit_tag_algo = 0;

		printf("%s(_,A,X,_):%d\n", av[1], commit_contains(&filter, A, X, &cache));
		clear_contains_cache(&cache);
	} else if (!strcmp(av[1], "get_reachable_subset")) {
		const int reachable_flag = 1;
		int i, count = 0;
		struct commit_list *current;
		struct commit_list *list = get_reachable_subset(X_array, X_nr,
								Y_array, Y_nr,
								reachable_flag);
		printf("get_reachable_subset(X,Y)\n");
		for (current = list; current; current = current->next) {
			if (!(list->item->object.flags & reachable_flag))
				die(_("commit %s is not marked reachable"),
				    oid_to_hex(&list->item->object.oid));
			count++;
		}
		for (i = 0; i < Y_nr; i++) {
			if (Y_array[i]->object.flags & reachable_flag)
				count--;
		}

		if (count < 0)
			die(_("too many commits marked reachable"));

		print_sorted_commit_ids(list);
		free_commit_list(list);
	}

	object_array_clear(&X_obj);
	strbuf_release(&buf);
	free_commit_list(X);
	free_commit_list(Y);
	free(X_array);
	free(Y_array);
	return 0;
}
