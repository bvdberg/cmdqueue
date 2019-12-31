UNAME=$(shell uname)

CCFLAGS=-Wall -Wextra -Wno-unused-parameter -Wshadow -std=c99 -O2 -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS

all: main test

remake: clean all

COMMON_SOURCES=cmdqueue.c list.c mycmdqueue.c
MAIN_SOURCES=main.c
TEST_SOURCES=mytests.c testmain.c
HEADERS=cmdqueue.h ctest.h list.h mycmdqueue.h util.h

test: $(COMMON_SOURCES) $(TEST_SOURCES) $(HEADERS)
	gcc $(CCFLAGS) $(COMMON_SOURCES) $(TEST_SOURCES) -o test -lpthread

main: $(COMMON_SOURCES) $(MAIN_SOURCES) $(HEADERS)
	gcc $(CCFLAGS) $(COMMON_SOURCES) $(MAIN_SOURCES) -o main -lpthread

clean:
	rm -f test main *.o

