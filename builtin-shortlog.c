#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "path-list.h"
#include "revision.h"
#include <string.h>

static const char shortlog_usage[] =
"git-shortlog [-n] [-s] [<commit-id>... ]\n";

static int compare_by_number(const void *a1, const void *a2)
{
	const struct path_list_item *i1 = a1, *i2 = a2;
	const struct path_list *l1 = i1->util, *l2 = i2->util;

	if (l1->nr < l2->nr)
		return -1;
	else if (l1->nr == l2->nr)
		return 0;
	else
		return +1;
}

static struct path_list_item mailmap_list[] = {
	{ "R.Marek@sh.cvut.cz", (void*)"Rudolf Marek" },
	{ "Ralf.Wildenhues@gmx.de", (void*)"Ralf Wildenhues" },
	{ "aherrman@de.ibm.com", (void*)"Andreas Herrmann" },
	{ "akpm@osdl.org", (void*)"Andrew Morton" },
	{ "andrew.vasquez@qlogic.com", (void*)"Andrew Vasquez" },
	{ "aquynh@gmail.com", (void*)"Nguyen Anh Quynh" },
	{ "axboe@suse.de", (void*)"Jens Axboe" },
	{ "blaisorblade@yahoo.it", (void*)"Paolo 'Blaisorblade' Giarrusso" },
	{ "bunk@stusta.de", (void*)"Adrian Bunk" },
	{ "domen@coderock.org", (void*)"Domen Puncer" },
	{ "dougg@torque.net", (void*)"Douglas Gilbert" },
	{ "dwmw2@shinybook.infradead.org", (void*)"David Woodhouse" },
	{ "ecashin@coraid.com", (void*)"Ed L Cashin" },
	{ "felix@derklecks.de", (void*)"Felix Moeller" },
	{ "fzago@systemfabricworks.com", (void*)"Frank Zago" },
	{ "gregkh@suse.de", (void*)"Greg Kroah-Hartman" },
	{ "hch@lst.de", (void*)"Christoph Hellwig" },
	{ "htejun@gmail.com", (void*)"Tejun Heo" },
	{ "jejb@mulgrave.(none)", (void*)"James Bottomley" },
	{ "jejb@titanic.il.steeleye.com", (void*)"James Bottomley" },
	{ "jgarzik@pretzel.yyz.us", (void*)"Jeff Garzik" },
	{ "johnpol@2ka.mipt.ru", (void*)"Evgeniy Polyakov" },
	{ "kay.sievers@vrfy.org", (void*)"Kay Sievers" },
	{ "minyard@acm.org", (void*)"Corey Minyard" },
	{ "mshah@teja.com", (void*)"Mitesh shah" },
	{ "pj@ludd.ltu.se", (void*)"Peter A Jonsson" },
	{ "rmps@joel.ist.utl.pt", (void*)"Rui Saraiva" },
	{ "santtu.hyrkko@gmail.com", (void*)"Santtu Hyrkk,Av(B" },
	{ "simon@thekelleys.org.uk", (void*)"Simon Kelley" },
	{ "ssant@in.ibm.com", (void*)"Sachin P Sant" },
	{ "terra@gnome.org", (void*)"Morten Welinder" },
	{ "tony.luck@intel.com", (void*)"Tony Luck" },
	{ "welinder@anemone.rentec.com", (void*)"Morten Welinder" },
	{ "welinder@darter.rentec.com", (void*)"Morten Welinder" },
	{ "welinder@troll.com", (void*)"Morten Welinder" }
};

static struct path_list mailmap = {
	mailmap_list,
	sizeof(mailmap_list) / sizeof(struct path_list_item), 0, 0
};

static int map_email(char *email, char *name, int maxlen)
{
	char *p;
	struct path_list_item *item;

	/* autocomplete common developers */
	p = strchr(email, '>');
	if (!p)
		return 0;

	*p = '\0';
	item = path_list_lookup(email, &mailmap);
	if (item != NULL) {
		const char *realname = (const char *)item->util;
		strncpy(name, realname, maxlen);
		return 1;
	}
	return 0;
}

static void insert_author_oneline(struct path_list *list,
		const char *author, int authorlen,
		const char *oneline, int onelinelen)
{
	const char *dot3 = "/pub/scm/linux/kernel/git/";
	char *buffer, *p;
	struct path_list_item *item;
	struct path_list *onelines;

	while (authorlen > 0 && isspace(author[authorlen - 1]))
		authorlen--;

	buffer = xmalloc(authorlen + 1);
	memcpy(buffer, author, authorlen);
	buffer[authorlen] = '\0';

	item = path_list_insert(buffer, list);
	if (item->util == NULL)
		item->util = xcalloc(1, sizeof(struct path_list));
	else
		free(buffer);

	if (!strncmp(oneline, "[PATCH", 6)) {
		char *eob = strchr(buffer, ']');

		while (isspace(eob[1]) && eob[1] != '\n')
			eob++;
		if (eob - oneline < onelinelen) {
			onelinelen -= eob - oneline;
			oneline = eob;
		}
	}

	while (onelinelen > 0 && isspace(oneline[0])) {
		oneline++;
		onelinelen--;
	}

	while (onelinelen > 0 && isspace(oneline[onelinelen - 1]))
		onelinelen--;

	buffer = xmalloc(onelinelen + 1);
	memcpy(buffer, oneline, onelinelen);
	buffer[onelinelen] = '\0';

	while ((p = strstr(buffer, dot3)) != NULL) {
		memcpy(p, "...", 3);
		strcpy(p + 2, p + sizeof(dot3) - 1);
	}


	onelines = item->util;
	if (onelines->nr >= onelines->alloc) {
		onelines->alloc = alloc_nr(onelines->nr);
		onelines->items = xrealloc(onelines->items,
				onelines->alloc
				* sizeof(struct path_list_item));
	}

	onelines->items[onelines->nr].util = NULL;
	onelines->items[onelines->nr++].path = buffer;
}

static void read_from_stdin(struct path_list *list)
{
	char buffer[1024];

	while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
		char *bob;
		if ((buffer[0] == 'A' || buffer[0] == 'a') &&
				!strncmp(buffer + 1, "uthor: ", 7) &&
				(bob = strchr(buffer + 7, '<')) != NULL) {
			char buffer2[1024], offset = 0;

			if (map_email(bob + 1, buffer, sizeof(buffer)))
				bob = buffer + strlen(buffer);
			else {
				offset = 8;
				while (isspace(bob[-1]))
					bob--;
			}

			while (fgets(buffer2, sizeof(buffer2), stdin) &&
					buffer2[0] != '\n')
				; /* chomp input */
			if (fgets(buffer2, sizeof(buffer2), stdin))
				insert_author_oneline(list,
						buffer + offset,
						bob - buffer - offset,
						buffer2, strlen(buffer2));
		}
	}
}

static void get_from_rev(struct rev_info *rev, struct path_list *list)
{
	char scratch[1024];
	struct commit *commit;

	prepare_revision_walk(rev);
	while ((commit = get_revision(rev)) != NULL) {
		char *author = NULL, *oneline, *buffer;
		int authorlen = authorlen, onelinelen;

		/* get author and oneline */
		for (buffer = commit->buffer; buffer && *buffer != '\0' &&
				*buffer != '\n'; ) {
			char *eol = strchr(buffer, '\n');

			if (eol == NULL)
				eol = buffer + strlen(buffer);
			else
				eol++;

			if (!strncmp(buffer, "author ", 7)) {
				char *bracket = strchr(buffer, '<');

				if (bracket == NULL || bracket > eol)
					die("Invalid commit buffer: %s",
					    sha1_to_hex(commit->object.sha1));

				if (map_email(bracket + 1, scratch,
							sizeof(scratch))) {
					author = scratch;
					authorlen = strlen(scratch);
				} else {
					while (bracket[-1] == ' ')
						bracket--;

					author = buffer + 7;
					authorlen = bracket - buffer - 7;
				}
			}
			buffer = eol;
		}

		if (author == NULL)
			die ("Missing author: %s",
					sha1_to_hex(commit->object.sha1));

		if (buffer == NULL || *buffer == '\0') {
			oneline = "<none>";
			onelinelen = sizeof(oneline) + 1;
		} else {
			char *eol;

			oneline = buffer + 1;
			eol = strchr(oneline, '\n');
			if (eol == NULL)
				onelinelen = strlen(oneline);
			else
				onelinelen = eol - oneline;
		}

		insert_author_oneline(list,
				author, authorlen, oneline, onelinelen);
	}

}

int cmd_shortlog(int argc, const char **argv, const char *prefix)
{
	struct rev_info rev;
	struct path_list list = { NULL, 0, 0, 1 };
	int i, j, sort_by_number = 0, summary = 0;

	init_revisions(&rev, prefix);
	argc = setup_revisions(argc, argv, &rev, NULL);
	while (argc > 1) {
		if (!strcmp(argv[1], "-n") || !strcmp(argv[1], "--numbered"))
			sort_by_number = 1;
		else if (!strcmp(argv[1], "-s") ||
				!strcmp(argv[1], "--summary"))
			summary = 1;
		else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
			usage(shortlog_usage);
		else
			die ("unrecognized argument: %s", argv[1]);
		argv++;
		argc--;
	}

	if (rev.pending.nr == 1)
		die ("Need a range!");
	else if (rev.pending.nr == 0)
		read_from_stdin(&list);
	else
		get_from_rev(&rev, &list);

	if (sort_by_number)
		qsort(list.items, sizeof(struct path_list_item), list.nr,
			compare_by_number);

	for (i = 0; i < list.nr; i++) {
		struct path_list *onelines = list.items[i].util;

		printf("%s (%d):\n", list.items[i].path, onelines->nr);
		if (!summary) {
			for (j = onelines->nr - 1; j >= 0; j--)
				printf("      %s\n", onelines->items[j].path);
			printf("\n");
		}

		onelines->strdup_paths = 1;
		path_list_clear(onelines, 1);
		free(onelines);
		list.items[i].util = NULL;
	}

	list.strdup_paths = 1;
	path_list_clear(&list, 1);

	return 0;
}

