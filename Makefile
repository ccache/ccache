CFLAGS=-W -Wall -O2
CC=gcc

OBJS= ccache.o mdfour.o hash.o execute.o util.o args.o
CLEAN_OBJS= ccache_clean.o util.o
HEADERS = ccache.h mdfour.h

all: ccache ccache_clean

ccache: $(OBJS) $(HEADERS)
	$(CC) -o $@ $(OBJS)

ccache_clean: $(CLEAN_OBJS) $(HEADERS)
	$(CC) -o $@ $(CLEAN_OBJS)

clean:
	/bin/rm -f $(OBJS) *~ ccache ccache_clean

