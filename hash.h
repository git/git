#ifndef HASH_H
#define HASH_H

#include "hash-ll.h"
#include "repository.h"

#ifdef USE_THE_REPOSITORY_VARIABLE
# define the_hash_algo the_repository->hash_algo
#endif

#endif
