CC=gcc
CFLAGS=-W -Wall -O2

OBJS= ccache.o mdfour.o hash.o execute.o util.o args.o stats.o cleanup.o
HEADERS = ccache.h mdfour.h

all: ccache

ccache: $(OBJS) $(HEADERS)
	$(CC) -o $@ $(OBJS)

clean:
	/bin/rm -f $(OBJS) *~ ccache

