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

void put_u24(byte *out, uint32_t i);
uint32_t get_u24(byte *in);

uint64_t get_u64(byte *in);
void put_u64(byte *out, uint64_t i);

void put_u32(byte *out, uint32_t i);
uint32_t get_u32(byte *in);

void put_u16(byte *out, uint16_t i);
uint16_t get_u16(byte *in);
int binsearch(int sz, int (*f)(int k, void *args), void *args);

void free_names(char **a);
void parse_names(char *buf, int size, char ***namesp);
int names_equal(char **a, char **b);
int names_length(char **names);

#endif
