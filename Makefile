CFLAGS=-W -Wall -g 
CC=gcc

OBJS= ccache.o mdfour.o hash.o execute.o util.o args.o
HEADERS = ccache.h mdfour.h

ccache: $(OBJS) $(HEADERS)
	$(CC) -o $@ $(OBJS)

clean:
	/bin/rm -f $(OBJS) *~ ccache
