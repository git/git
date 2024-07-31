#
# Copyright (c) Vicent Marti. All rights reserved.
#
# This file is part of clar, distributed under the ISC license.
# For full terms see the included COPYING file.
#

#
# Set up the path to the clar sources and to the fixtures directory
#
# The fixture path needs to be an absolute path so it can be used
# even after we have chdir'ed into the test directory while testing.
#
CURRENT_MAKEFILE  := $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST))
TEST_DIRECTORY    := $(abspath $(dir $(CURRENT_MAKEFILE)))
CLAR_PATH         := $(dir $(TEST_DIRECTORY))
CLAR_FIXTURE_PATH := $(TEST_DIRECTORY)/resources/

CFLAGS=-g -I.. -I. -Wall -DCLAR_FIXTURE_PATH=\"$(CLAR_FIXTURE_PATH)\"

.PHONY: clean

# list the objects that go into our test
objects = main.o sample.o

# build the test executable itself
clar_test: $(objects) clar_test.h clar.suite $(CLAR_PATH)clar.c
	$(CC) $(CFLAGS) -o $@ "$(CLAR_PATH)clar.c" $(objects)

# test object files depend on clar macros
$(objects) : $(CLAR_PATH)clar.h

# build the clar.suite file of test metadata
clar.suite:
	python "$(CLAR_PATH)generate.py" .

# remove all generated files
clean:
	$(RM) -rf *.o clar.suite .clarcache clar_test clar_test.dSYM
