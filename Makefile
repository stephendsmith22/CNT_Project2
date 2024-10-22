CC=gcc

OS := $(shell uname -s)

# Extra LDFLAGS if Solaris
ifeq ($(OS), SunOS)
	LDFLAGS=-lsocket -lnsl
    endif

all: client server 

client: client.c
	$(CC) client.c -o runclient -lcrypto

server: server.c
	$(CC) server.c -o runserver -lcrypto

clean:
	    rm -f client server *.o

