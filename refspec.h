#ifndef REFSPEC_H
#define REFSPEC_H

#define TAG_REFSPEC "refs/tags/*:refs/tags/*"
extern const struct refspec *tag_refspec;

struct refspec {
	unsigned force : 1;
	unsigned pattern : 1;
	unsigned matching : 1;
	unsigned exact_sha1 : 1;

	char *src;
	char *dst;
};

int valid_fetch_refspec(const char *refspec);
struct refspec *parse_fetch_refspec(int nr_refspec, const char **refspec);
struct refspec *parse_push_refspec(int nr_refspec, const char **refspec);

void free_refspec(int nr_refspec, struct refspec *refspec);

#endif /* REFSPEC_H */
