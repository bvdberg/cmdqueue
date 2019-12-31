CCFLAGS=-Wall -Wextra -Wno-unused-parameter -Wshadow -std=c99 -O2 -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS

COMMON_SOURCES=cmdqueue.c list.c mycmdqueue.c
MAIN_SOURCES=main.c
TEST_SOURCES=test/mytests.c test/testmain.c
HEADERS=cmdqueue.h test/ctest.h list.h mycmdqueue.h util.h

all: run testrunner

remake: clean all

run: $(COMMON_SOURCES) $(MAIN_SOURCES) $(HEADERS)
	@ gcc $(CCFLAGS) $(COMMON_SOURCES) $(MAIN_SOURCES) -o run -lpthread

testrunner: $(COMMON_SOURCES) $(TEST_SOURCES) $(HEADERS)
	@ gcc -I. $(CCFLAGS) $(COMMON_SOURCES) $(TEST_SOURCES) -o test/runner -lpthread

clean:
	@ rm -f test/runner run

