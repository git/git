#ifndef NOTES_H
#define NOTES_H

/* Free (and de-initialize) the internal notes tree structure */
void free_notes(void);

#define NOTES_SHOW_HEADER 1
#define NOTES_INDENT 2

void get_commit_notes(const struct commit *commit, struct strbuf *sb,
		const char *output_encoding, int flags);

#endif
