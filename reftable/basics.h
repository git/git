/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef BASICS_H
#define BASICS_H

#include "system.h"

#include "reftable.h"

#define true 1
#define false 0

void put_be24(byte *out, uint32_t i);
uint32_t get_be24(byte *in);
void put_be16(uint8_t *out, uint16_t i);

int binsearch(int sz, int (*f)(int k, void *args), void *args);

void free_names(char **a);
void parse_names(char *buf, int size, char ***namesp);
int names_equal(char **a, char **b);
int names_length(char **names);

void *reftable_malloc(size_t sz);
void *reftable_realloc(void *p, size_t sz);
void reftable_free(void *p);
void *reftable_calloc(size_t sz);

#endif
