CC = gcc
CFLAGS = -std=c99 -O1 -Wall -Iincludes
LDFLAGS = -lpthread

UTILS = socket.c logging.c io.c http.c cache.c header_list.c

all: proxy tests

proxy:
	$(CC) $(CFLAGS) proxy.c $(patsubst %, util/%, $(UTILS)) -o proxy $(LDFLAGS)

tests:
	$(CC) $(CFLAGS) tests.c $(patsubst %, util/%, $(UTILS)) -o tests $(LDFLAGS)

clean:
	rm -f *~ *.o proxy tests

