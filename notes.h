#ifndef NOTES_H
#define NOTES_H

void get_commit_notes(const struct commit *commit, struct strbuf *sb,
		const char *output_encoding);

#endif
