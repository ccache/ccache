CC=gcc
CFLAGS=-W -Wall -O2

OBJS= ccache.o mdfour.o hash.o execute.o util.o args.o stats.o cleanup.o
HEADERS = ccache.h mdfour.h

all: ccache ccache.1

ccache: $(OBJS) $(HEADERS)
	$(CC) -o $@ $(OBJS)

ccache.1: ccache.yo
	yodl2man -o ccache.1 ccache.yo

clean:
	/bin/rm -f $(OBJS) *~ ccache
