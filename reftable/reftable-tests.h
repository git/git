/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef REFTABLE_TESTS_H
#define REFTABLE_TESTS_H

int basics_test_main(int argc, const char **argv);
int block_test_main(int argc, const char **argv);
int merged_test_main(int argc, const char **argv);
int pq_test_main(int argc, const char **argv);
int record_test_main(int argc, const char **argv);
int refname_test_main(int argc, const char **argv);
int readwrite_test_main(int argc, const char **argv);
int stack_test_main(int argc, const char **argv);
int tree_test_main(int argc, const char **argv);
int reftable_dump_main(int argc, char *const *argv);

#endif
