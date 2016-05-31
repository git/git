#ifndef TRAILER_H
#define TRAILER_H

void process_trailers(const char *file, int in_place, int trim_empty,
		      struct string_list *trailers);

#endif /* TRAILER_H */
