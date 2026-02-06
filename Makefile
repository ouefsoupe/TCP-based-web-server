CC = gcc
CFLAGS = -Wall -g
# macOS doesn't need -lnsl, -lrt, or -lsocket (those are for Solaris/Linux)
LDFLAGS = -lpthread

OBJS = nethelp.o server.o

all: server

nethelp.o: nethelp.c
	$(CC) $(CFLAGS) -c nethelp.c

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

server: server.o nethelp.o
	$(CC) $(LDFLAGS) -o $@ server.o nethelp.o

clean:
	rm -f server *.o *~ core
