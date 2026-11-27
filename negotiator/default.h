#ifndef NEGOTIATOR_DEFAULT_H
#define NEGOTIATOR_DEFAULT_H

struct fetch_negotiator;
struct repository;

void default_negotiator_init(struct repository *r,
			     struct fetch_negotiator *negotiator);

#endif
